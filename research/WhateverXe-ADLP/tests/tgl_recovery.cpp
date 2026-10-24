#include "../WhateverXe/kern_tgl_recovery.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace {
alignas(16) uint8_t channel[0x180],affected[0x180],ring[0x100],context[0xD0];
alignas(16) uint8_t owner[0x1300],scheduler[0x200],otherScheduler[0x200];
alignas(16) uint8_t ringVT[0x200],schedulerVT[0x200];
unsigned resets,resumes,stage;bool nativeOK,replaceScheduler;
bool reset(void *r,void *c) {
	assert(r==ring && c==context && stage++==0);++resets;
	// Native side effects must survive. No caller-synthesized completion.
	TGLRecovery::field<uint32_t>(affected,0x178)=0xABCDEF;
	if (replaceScheduler) TGLRecovery::field<void *>(owner,0x1250)=otherScheduler;
	return nativeOK;
}
void resume(void *s) {
	assert(s==(replaceScheduler ? otherScheduler : scheduler) && stage++==1);++resumes;
}
}
int main() {
	for (bool ok:{false,true}) for (bool replace:{false,true}) for (unsigned initial:{0U,5U,0xFFFFFFFFU}) {
		memset(channel,0,sizeof(channel));memset(affected,0,sizeof(affected));
		TGLRecovery::field<void *>(channel,0x130)=ring;
		TGLRecovery::field<void *>(channel,0x18)=owner;
		TGLRecovery::field<void *>(ring,0)=ringVT;
		TGLRecovery::field<void *>(ring,0x20)=context;
		TGLRecovery::field<void *>(context,0xB8)=affected;
		TGLRecovery::field<void *>(owner,0x1250)=scheduler;
		TGLRecovery::field<void *>(scheduler,0)=schedulerVT;
		TGLRecovery::field<void *>(otherScheduler,0)=schedulerVT;
		TGLRecovery::field<decltype(&reset)>(ringVT,0x168)=reset;
		TGLRecovery::field<decltype(&resume)>(schedulerVT,0x180)=resume;
		TGLRecovery::field<uint32_t>(affected,0x20)=2;
		TGLRecovery::field<uint32_t>(affected,0x17C)=initial;
		nativeOK=ok;replaceScheduler=replace;resets=resumes=stage=0;
		const auto result=TGLRecovery::resetAndResume(channel);
		assert(resets==1 && resumes==1 && stage==2 && result.resetOK==ok);
		assert(result.affected==reinterpret_cast<uintptr_t>(affected) && result.index==2);
		assert(result.failuresBefore==initial && result.failuresAfter==initial+unsigned(!ok));
		assert(TGLRecovery::field<uint32_t>(affected,0x178)==0xABCDEF);
		// Affected channel comes from the context; do not update caller blindly.
		assert(TGLRecovery::field<uint32_t>(channel,0x17C)==0);
	}
	puts("12 recovery cases passed: native result, count, order, affected channel, owner reload");
}
