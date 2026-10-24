// WhateverGreen 24659d6, BSD-3-Clause; regression counterexample only.
bool XE::tglDekelAccess(void *port, uint32_t index, bool write, uint32_t &value) {
	static const uint32_t expected[] = {
		0x20D4, 0x20D8, 0x2130, 0x212C, 0x46030, 0x2200, 0x2204, 0x2208,
		0x168210, 0x168214, 0x168218, 0x220C, 0x2244, 0x224C,
		0x2C0, 0x2C4, 0x2C8, 0x12C0, 0x12C4, 0x12C8, 0xA0, 0x10A0
	};
	auto bytes = static_cast<uint8_t *>(port);
	auto access = *reinterpret_cast<uint8_t **>(bytes + 0x578);
	const auto mode = *reinterpret_cast<uint32_t *>(bytes + 0x550);
	if (!access || mode != 4 || index < 0x34 || index > 0x49 || !callbackXE->tglDekelLock) return false;
	const auto raw = *reinterpret_cast<uint32_t *>(bytes + 0x580 + index * 4);
	if (raw != expected[index - 0x34]) return false;
	auto base = *reinterpret_cast<volatile uint8_t **>(access + 0x50);
	const auto size = *reinterpret_cast<uint32_t *>(access + 0x60);
	if (!base || size < 0x16A000) return false;
	// RBR/HBR use A-divider10 in Linux's hardware-proven DKL DP setup.
	// The donor writes5 for these too. HBR2 remains5; preserve other fields.
	if (write && index == 0x35) {
		const auto rate = *reinterpret_cast<uint32_t *>(bytes + 0x128);
		const auto requested = value;
		if ((rate == 6 || rate == 10) && (value & 0xFF00U) == 0x500)
			value = (value & ~0xFF00U) | 0xA00;
		uint32_t divider[] = {rate, requested, value};
		char key[48]; snprintf(key, sizeof(key), "TGLCoreDividerRate%u", rate);
		if (!tglQuietTraining()) IORegistryEntry::getRegistryRoot()->setProperty(key, divider, sizeof(divider));
	}
	uint32_t internal = raw;
	if (index >= 0x3C && index <= 0x3E) internal = 0x2210 + (index - 0x3C) * 4;
	const bool direct = index == 0x38;
	const uint32_t offset = direct ? 0x46034 : 0x169000 + (internal & 0xFFF);
	const uint32_t bank = direct ? 0 : (internal >> 12) & 0xF;
	// Linux tgl_dkl_phy_set_signal_levels clears PMD lane suspension before
	// DPCNTL0/1. Firmware leaves both banks at 0xC when HDMI owns boot output;
	// the donor otherwise inherits 0 from firmware's active DP setup (r322).
	const bool signalWrite = write && (index == 0x42 || index == 0x43 || index == 0x45 || index == 0x46);
	uint32_t suspendBefore = 0, suspendAfter = 0;
	// No allocation or registry publication while interrupts are disabled.
	const auto irq = IOSimpleLockLockDisableInterrupt(callbackXE->tglDekelLock);
	if (!direct) {
		auto hip = reinterpret_cast<volatile uint32_t *>(base + 0x1010A0);
		*hip = (*hip & ~0xF00U) | (bank << 8);
		(void)*hip;
	}
	if (signalWrite) {
		auto suspend = reinterpret_cast<volatile uint32_t *>(base + 0x169D00);
		suspendBefore = *suspend;
		if (suspendBefore) *suspend = 0;
		suspendAfter = *suspend;
	}
	auto reg = reinterpret_cast<volatile uint32_t *>(base + offset);
	// r321: DKL PLL fields are RMW, unlike the donor's full-register writes.
	// Preserve AFC startup / factory trim / reserved fields, as Linux v6.12
	// dkl_pll_write() does when no VBT AFC override is requested.
	uint32_t pllMask = 0;
	switch (index) {
		case 0x39: pllMask = 0x001FFFFFU; break; // DIV0: integ/prop/prediv/int
		case 0x3A: pllMask = 0x001F00FFU; break; // DIV1: IREF trim, TDC count
		case 0x3C: pllMask = 0xE0FF3A00U; break; // SSC: ndiv/length/number/enable
		case 0x3D: pllMask = 0x7FFFFF00U; break; // BIAS: frac enable/divider
		case 0x3E: pllMask = 0x0000FFFFU; break; // TDC: SSC step, feed-forward
	}
	const auto requested = value;
	uint32_t before = 0, after = 0;
	if (write) {
		if (pllMask) {
			before = *reg;
			value = (before & ~pllMask) | (value & pllMask);
		}
		*reg = value;
		after = *reg;
	} else value = *reg;
	IOSimpleLockUnlockEnableInterrupt(callbackXE->tglDekelLock, irq);
	if (tglQuietTraining()) return true;
	if (signalWrite) {
		uint32_t record[] = {index, bank, suspendBefore, suspendAfter};
		char key[48]; snprintf(key, sizeof(key), "TGLDklLaneResumeFirst%u", bank);
		auto root = IORegistryEntry::getRegistryRoot();
		if (!root->getProperty(key)) root->setProperty(key, record, sizeof(record));
		snprintf(key, sizeof(key), "TGLDklLaneResumeLast%u", bank);
		root->setProperty(key, record, sizeof(record));
	}
	if (write && pllMask) {
		uint32_t correction[] = {index, pllMask, before, requested, value, after};
		char key[48]; snprintf(key, sizeof(key), "TGLDekelPLLRMW%02x", index);
		auto root = IORegistryEntry::getRegistryRoot();
		root->setProperty(key, correction, sizeof(correction));
		snprintf(key, sizeof(key), "TGLDekelPLLRMWFirst%02x", index);
		if (!root->getProperty(key)) root->setProperty(key, correction, sizeof(correction));
	}
	static uint32_t counts[22][2] {};
	const auto count = __sync_add_and_fetch(&counts[index - 0x34][write], 1);
	uint32_t record[] = {count, raw, offset, bank, value};
	char key[48]; snprintf(key, sizeof(key), "TGLDekel%02x%s", index, write ? "Write" : "Read");
	IORegistryEntry::getRegistryRoot()->setProperty(key, record, sizeof(record));
	if (write) {
		static uint32_t sequence {};
		const auto seq = __sync_add_and_fetch(&sequence, 1);
		uint32_t event[] = {seq, index, count, raw, offset, bank, value};
		snprintf(key, sizeof(key), "TGLDekelWriteLast%u", seq % 128);
		IORegistryEntry::getRegistryRoot()->setProperty(key, event, sizeof(event));
		if (seq <= 64) {
			snprintf(key, sizeof(key), "TGLDekelWriteFirst%u", seq);
			IORegistryEntry::getRegistryRoot()->setProperty(key, event, sizeof(event));
		}
	}
	return true;
}
