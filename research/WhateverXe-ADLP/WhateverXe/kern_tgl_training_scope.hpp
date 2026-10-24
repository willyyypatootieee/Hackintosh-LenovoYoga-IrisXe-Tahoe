// Diagnostic suppression during native link training. No allocation or MMIO.
#ifndef kern_tgl_training_scope_hpp
#define kern_tgl_training_scope_hpp

#include <stdint.h>

class TGLTrainingScopes {
public:
	static constexpr uint32_t Capacity = 32;
	// Each invocation owns a slot, including nested calls on the same thread.
	uint32_t enter(uintptr_t thread) {
		for (uint32_t i = 0; i < Capacity; ++i)
			if (__sync_bool_compare_and_swap(&threads[i], 0, thread)) return i;
		// If unexpectedly saturated, suppress diagnostics globally until the
		// overflow calls finish. Never resume intrusive snapshots mid-training.
		__sync_add_and_fetch(&overflow, 1);
		return Capacity;
	}
	void leave(uint32_t token) {
		if (token < Capacity) __sync_lock_release(&threads[token]);
		else __sync_sub_and_fetch(&overflow, 1);
	}
	bool active(uintptr_t thread) {
		if (__sync_fetch_and_add(&overflow, 0)) return true;
		for (uint32_t i = 0; i < Capacity; ++i)
			if (__sync_fetch_and_add(&threads[i], 0) == thread) return true;
		return false;
	}
private:
	uintptr_t threads[Capacity] {};
	uint32_t overflow {};
};
#endif
