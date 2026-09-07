from pathlib import Path
import subprocess
import tempfile
import resource
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
test=r'''
#include "chat/domain/persistent_packet_id_allocator.h"
#include <cassert>
#include <cstdint>
#include <iostream>
using chat::PersistentPacketIdAllocator;
int main(){
 const uint64_t end=1ULL<<32;uint32_t id=999;uint64_t saved=1;int writes=0;
 auto commit=[&](uint64_t n){assert(n>saved);saved=n;++writes;return true;};
 PersistentPacketIdAllocator a(saved);
 assert(a.allocate(0,commit,id)&&id==1&&saved==1025&&writes==1);
 assert(a.allocate(0,commit,id)&&id==2&&writes==1);
 assert(!a.allocate(1,commit,id)&&id==0&&writes==1);
 assert(a.allocate(1024,commit,id)&&id==1024&&writes==1);
 assert(a.allocate(0,commit,id)&&id==1025&&saved==2049&&writes==2);
 PersistentPacketIdAllocator reboot(saved);
 assert(reboot.allocate(0,commit,id)&&id==2049&&saved==3073);
 assert(reboot.allocate(5000,commit,id)&&id==5000&&saved==6024);
 assert(!reboot.allocate(4999,commit,id)&&id==0);
 PersistentPacketIdAllocator fail(1);int failures=0;
 auto refuse=[&](uint64_t n){assert(n==1025);++failures;return false;};
 assert(!fail.allocate(0,refuse,id)&&id==0&&failures==1);
 assert(!fail.allocate(0,refuse,id)&&id==0&&failures==2);
 assert(fail.allocate(0,[](uint64_t n){return n==1025;},id)&&id==1);
 // Commit can take effect despite reporting an I/O error: no packet may be issued.
 uint64_t uncertain_saved=1;PersistentPacketIdAllocator uncertain(1);
 assert(!uncertain.allocate(0,[&](uint64_t n){uncertain_saved=n;return false;},id)&&id==0);
 PersistentPacketIdAllocator after_uncertain(uncertain_saved);
 assert(after_uncertain.allocate(0,[](uint64_t n){return n==2049;},id)&&id==1025);
 // Reboot immediately after a successful reservation skips even an unused range.
 PersistentPacketIdAllocator last(end-1);int last_writes=0;
 assert(last.allocate(0,[&](uint64_t n){++last_writes;return n==end;},id)&&id==UINT32_MAX);
 assert(!last.allocate(0,[](uint64_t){assert(false);return true;},id)&&id==0&&last_writes==1);
 for(uint64_t marker:{uint64_t(0),end,end+1,UINT64_MAX}){
  PersistentPacketIdAllocator bad(marker);
  assert(!bad.allocate(0,[](uint64_t){assert(false);return true;},id)&&id==0);
  assert(!bad.allocate(7,[](uint64_t){assert(false);return true;},id)&&id==0);
 }
 PersistentPacketIdAllocator jump(1);
 assert(jump.allocate(UINT32_MAX,[&](uint64_t n){return n==end;},id)&&id==UINT32_MAX);
 assert(!jump.allocate(UINT32_MAX,[](uint64_t){assert(false);return true;},id)&&id==0);
 // Consume two full ranges: one commit per range, not one per packet.
 uint64_t bound=1;int commits=0;PersistentPacketIdAllocator ranges(bound);
 for(uint32_t n=1;n<=2048;++n){assert(ranges.allocate(0,[&](uint64_t hi){assert(hi>bound);bound=hi;++commits;return true;},id)&&id==n);assert(uint64_t(id)<bound);}
 assert(commits==2&&bound==2049);
 std::cout<<"packet allocation checks PASS\n";
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(test)
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-fuse-ld=bfd','-Imodules/core_chat/include',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
