// Tiger Lake host-scheduler CSB intake. Opt-in donor hooks use -tglcsb12.
// Contract: Intel TGL PRM vol.8 / vol.2c and Linux v6.12 execlists CSB handling.
#ifndef kern_tgl_csb_hpp
#define kern_tgl_csb_hpp

#include <stdint.h>

namespace TGLCSB {
constexpr uint32_t Capacity = 12;
constexpr uint32_t ResetIndex = Capacity - 1;
constexpr uint64_t Sentinel = UINT64_MAX;

// Mask only the Gen11+ read/write fields defined in intel_lrc_reg.h.
// r92's broad 0xffff mask also cleared bits15:12 (observed 7 -> 0),
// after which HWSP and indexed-MMIO events appeared one slot apart.
// Preserve the other fields; their TGL meaning is not established here.
constexpr uint32_t pointerResetWrite(uint32_t index) {
	return 0x0F0F0000U | (index << 8) | index;
}

// TGL format only; Xe_HP has different bit positions. A context identity has
// both an 11-bit ID and a 6-bit submission counter. Never infer lite restore
// from the ID alone, or from a zero low nibble in the donor's translated flags.
struct Identity {
	uint16_t id;
	uint8_t counter;
	bool idle() const { return id == 0x7FF; }
	bool operator==(const Identity &other) const {
		return id == other.id && counter == other.counter;
	}
};

enum class Kind : uint8_t {
	Invalid, IdleToActive, PreemptToIdle, IdlePreempt,
	NewQueueComplete, NewQueueWait, NewQueuePreempt, LiteRestore,
	ElementComplete, ElementWait,
	ActiveToIdleComplete, ActiveToIdleWait, ActiveToIdlePreempt
};

struct Event {
	uint64_t raw;
	Identity away, to;
	uint8_t detail, waitDetail, engineClass, engineInstance;
	bool newQueue, semaphoreWaitMode;
	Kind kind {Kind::Invalid};
};

// Intel TGL PRM vol.8 Context Status table, cases1..10. This classifies the
// immutable raw sample only. It DOES NOT retire work, alter pending queues,
// normalize identities, or select a legacy handler. In particular preempt to
// idle must not be turned into new-queue promotion or inferred completion.
inline Event classify(uint64_t raw) {
	const auto lo = static_cast<uint32_t>(raw);
	const auto hi = static_cast<uint32_t>(raw >> 32);
	Event event {raw,
		{static_cast<uint16_t>((hi >> 15) & 0x7FF), static_cast<uint8_t>(hi >> 26)},
		{static_cast<uint16_t>((lo >> 15) & 0x7FF), static_cast<uint8_t>(lo >> 26)},
		static_cast<uint8_t>(hi & 15), static_cast<uint8_t>((hi >> 5) & 127),
		static_cast<uint8_t>((lo >> 3) & 7), static_cast<uint8_t>((lo >> 6) & 63),
		(lo & 1) != 0, (lo & 4) != 0, Kind::Invalid};
	if (raw == Sentinel)
		return event;
	if (event.newQueue) {
		if (event.away.idle()) {
			// PRM3 switch detail is X; no active context can be retired.
			if (event.to.idle()) event.kind = Kind::IdlePreempt;
			else if (!event.detail) event.kind = Kind::IdleToActive;
		} else if (event.to.idle()) {
			if (event.detail <= 5) event.kind = Kind::PreemptToIdle;
		} else if (event.away == event.to) {
			// PRM7 switch detail is N/A; same submission continues.
			event.kind = Kind::LiteRestore;
		} else if (!event.detail) event.kind = Kind::NewQueueComplete;
		else if (event.detail <= 4) event.kind = Kind::NewQueueWait;
		else if (event.detail == 5) event.kind = Kind::NewQueuePreempt;
	} else if (!event.away.idle()) {
		if (event.to.idle()) {
			if (!event.detail) event.kind = Kind::ActiveToIdleComplete;
			else if (event.detail <= 4) event.kind = Kind::ActiveToIdleWait;
			else if (event.detail == 5) event.kind = Kind::ActiveToIdlePreempt;
		} else if (!(event.away == event.to)) {
			if (!event.detail) event.kind = Kind::ElementComplete;
			else if (event.detail <= 4) event.kind = Kind::ElementWait;
		}
	}
	return event;
}

struct LegacyEntry { uint32_t flags, identity; };

// Preserve the exact donor entry ABI, but derive every field from ONE snapshot.
// A handler is selected from Event::kind, never BSF(flags & 15). This conversion
// is not permission to dispatch: the raw identity and queue state need validation.
inline LegacyEntry legacyEntry(const Event &event) {
	const uint32_t lo = static_cast<uint32_t>(event.raw);
	const uint32_t hi = static_cast<uint32_t>(event.raw >> 32);
	const bool awayValid = !event.away.idle(), toValid = !event.to.idle();
	const bool sameSubmission = event.away == event.to;
	uint32_t flags = !awayValid && toValid ? 1 : 0;
	if (event.detail == 5 || (awayValid && event.newQueue)) flags |= 2;
	if (awayValid && toValid && !sameSubmission && !event.newQueue) flags |= 4;
	if (awayValid && !toValid && !event.newQueue) flags |= 8;
	if (awayValid && !event.detail) flags |= 0x10;
	if (event.detail == 1) flags |= 0x20;
	if (event.detail == 2) flags |= 0x40;
	if (event.detail == 3) flags |= 0x100;
	if (event.detail == 4) flags |= 0x80;
	if (awayValid && sameSubmission) flags |= 0x8000;
	flags |= (lo & 4) << 9;
	const uint32_t identity = ((lo & 0xF8) << 26) | ((lo << 10) & 0x3F0000)
		| ((hi >> 3) & 0x1F800000) | ((hi >> 10) & 0xFFE0) | ((hi >> 12) & 7);
	return {flags, identity};
}

enum class Handler : uint8_t {
	Invalid, IdleToActive, NewQueue, ElementSwitch, ActiveToIdle,
	IdlePreempt, IncompleteToIdle, IncompleteElementSwitch
};

// The native handlers retain detail4 in their dependency wait queue and skip
// queue-capacity retirement. Only qualify signal mode; polling switches and
// other unfinished causes require separate evidence. The caller also checks
// owner flags: 0x18000 disables that native wait-queue path.
inline bool nativeSemaphoreWait(const Event &event) {
	return event.detail == 4 && event.semaphoreWaitMode &&
		(event.kind == Kind::ActiveToIdleWait || event.kind == Kind::ElementWait);
}

inline Handler handlerFor(const Event &event) {
	switch (event.kind) {
		case Kind::IdleToActive: return Handler::IdleToActive;
		case Kind::NewQueueComplete: case Kind::NewQueueWait:
		case Kind::NewQueuePreempt: case Kind::LiteRestore: return Handler::NewQueue;
		case Kind::ElementComplete: return Handler::ElementSwitch;
		case Kind::ElementWait: return nativeSemaphoreWait(event)
			? Handler::ElementSwitch : Handler::IncompleteElementSwitch;
		case Kind::ActiveToIdleComplete: return Handler::ActiveToIdle;
		case Kind::IdlePreempt: return Handler::IdlePreempt;
		// Existing ActiveToIdle retires work for waits other than semaphore wait;
		// existing Preempted promotes a pending bank. Neither is a general handler
		// for these cases. They need explicit unfinished-work/idle accounting.
		case Kind::ActiveToIdleWait: return nativeSemaphoreWait(event)
			? Handler::ActiveToIdle : Handler::IncompleteToIdle;
		case Kind::ActiveToIdlePreempt:
		case Kind::PreemptToIdle: return Handler::IncompleteToIdle;
		case Kind::Invalid: return Handler::Invalid;
	}
	return Handler::Invalid;
}

enum class Error : uint8_t { None, NotInitialized, InvalidPointer, Unavailable, Rejected };
enum class Source : uint8_t { HWSP, RetriedHWSP, MMIO };

struct Sample {
	uint64_t raw {Sentinel};
	uint32_t index {};
	uint8_t retries {};
	Source source {Source::HWSP};
};

struct State {
	uint32_t head {ResetIndex};
	Error error {Error::NotInitialized};
};

// Offset relative to the first CSB MMIO bank, not the engine base.
constexpr uint32_t mmioOffset(uint32_t index) {
	return index < 6 ? index * 8 : 0x50 + (index - 6) * 8;
}

// Caller holds the streamer's existing serialization and has quiesced the GPU.
// IO resetPointers must use the masked register write and a posting read; flush
// must finish CPU writes to HWSP. It must NOT reset contexts or retire work.
// The native reset's context ownership operations still have to run separately.
template<class IO>
void initialize(State &state, IO &io) {
	state.error = Error::NotInitialized;
	io.resetPointers(ResetIndex);
	io.writeTail(ResetIndex);
	for (uint32_t i = 0; i < Capacity; ++i)
		io.writeEntry(i, Sentinel);
	io.flushEntriesAndTail();
	io.resetPointers(ResetIndex);
	state.head = ResetIndex;
	state.error = Error::None;
}

// IO readEntry is one aligned 64-bit HWSP load. nowNs is monotonic; delayUs(1)
// must not sleep while an interrupt lock is held. Both a time limit and a poll
// limit bound retry; this is at most ten 1us delays, not a wall-clock guarantee.
// IO readMMIO64 uses the indexed register pair, NEVER the FIFO/pop register.
template<class IO>
Sample read(uint32_t index, IO &io) {
	Sample sample;
	sample.index = index;
	if (index >= Capacity)
		return sample;
	sample.raw = io.readEntry(index);
	if (sample.raw != Sentinel)
		return sample;
	const uint64_t start = io.nowNs();
	while (sample.raw == Sentinel && sample.retries < 10) {
		const uint64_t elapsed = io.nowNs() - start;
		if (elapsed >= 10000)
			break;
		io.delayUs(1);
		++sample.retries;
		sample.raw = io.readEntry(index);
	}
	sample.source = Source::RetriedHWSP;
	if (sample.raw == Sentinel) {
		sample.source = Source::MMIO;
		sample.raw = io.readMMIO64(mmioOffset(index));
	}
	return sample;
}

// readTail returns a decoded index (low 4 bits for the MMIO pointer path).
// readFence must order the tail load before the first entry load (x86 lfence).
// accept consumes the immutable Sample, validates identity/queue ownership, and
// then dispatches the native handler. False means terminal inconsistency: the
// caller must take a justified native failure/reset path, never retry dispatch.
// This helper neither translates an event nor chooses a native handler.
template<class IO, class Consumer>
Error drain(State &state, IO &io, Consumer &accept) {
	if (state.error != Error::None)
		return state.error;
	const uint32_t tail = io.readTail();
	if (tail >= Capacity || state.head >= Capacity)
		return state.error = Error::InvalidPointer;
	if (state.head == tail)
		return Error::None;
	io.readFence();
	while (state.head != tail) {
		const uint32_t next = state.head + 1 == Capacity ? 0 : state.head + 1;
		const auto sample = read(next, io);
		if (sample.raw == Sentinel)
			return state.error = Error::Unavailable;
		// Poison BEFORE dispatch can submit more GPU work and cause slot reuse.
		// The integration must preserve the submission-side write barrier.
		io.writeEntry(next, Sentinel);
		state.head = next;
		if (!accept(sample))
			return state.error = Error::Rejected;
	}
	return Error::None;
}
}
#endif
