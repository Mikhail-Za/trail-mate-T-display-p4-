#!/usr/bin/env python3
"""Exercise production NVS allocation with fault-injected storage/hash substitutes."""
from pathlib import Path
import resource
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
NVS = r"""
#pragma once
#include <cstddef>
using nvs_handle_t = unsigned;
using esp_err_t = int;
constexpr int ESP_OK=0, ESP_ERR_NVS_NOT_FOUND=1, NVS_READWRITE=2;
int nvs_open(const char*, int, nvs_handle_t*);
int nvs_get_blob(nvs_handle_t, const char*, void*, size_t*);
int nvs_set_blob(nvs_handle_t, const char*, const void*, size_t);
int nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
"""
SHA = r"""
#pragma once
#include <cstddef>
int mbedtls_sha256(const unsigned char*, size_t, unsigned char*, int);
"""
TEST = r"""
#include "platform/esp/radio/persistent_packet_ids.h"
#include "chat/domain/channel_hash.h"
#include "nvs.h"
#include "mbedtls/sha256.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
using namespace platform::esp::radio;
std::vector<unsigned char> saved, pending;
bool fail_open=false, fail_set=false, fail_commit=false, fail_sha=false;
int fail_get_call=0, get_calls=0, set_calls=0, commits=0, opens=0, closes=0;
constexpr uint32_t NODE=0x12345678;
constexpr uint64_t END=1ULL<<32;
int nvs_open(const char* ns,int mode,nvs_handle_t* h){
 assert(std::strcmp(ns,"tm_pkt_ids")==0 && mode==NVS_READWRITE);
 if(fail_open)return -1;
 *h=7;++opens;return ESP_OK;
}
int nvs_get_blob(nvs_handle_t h,const char* k,void* out,size_t* n){
 assert(h==7 && std::strcmp(k,"state_v1")==0);++get_calls;
 if(fail_get_call==get_calls)return -1;
 if(saved.empty())return ESP_ERR_NVS_NOT_FOUND;
 if(out){if(*n<saved.size())return -1;std::memcpy(out,saved.data(),saved.size());}
 *n=saved.size();return ESP_OK;
}
int nvs_set_blob(nvs_handle_t h,const char* k,const void* p,size_t n){
 assert(h==7 && std::strcmp(k,"state_v1")==0 && n==273);++set_calls;
 if(fail_set)return -1;
 auto bytes=static_cast<const unsigned char*>(p);pending.assign(bytes,bytes+n);return ESP_OK;
}
int nvs_commit(nvs_handle_t h){
 assert(h==7);saved=pending;++commits;return fail_commit?-1:ESP_OK;
}
void nvs_close(nvs_handle_t h){assert(h==7);++closes;}
int mbedtls_sha256(const unsigned char* p,size_t n,unsigned char* out,int is224){
 assert(p && (n==16 || n==32) && is224==0);
 if(fail_sha)return -1;
 // Observable deterministic substitute, NOT a cryptographic implementation.
 for(size_t i=0;i<32;++i)out[i]=static_cast<unsigned char>(p[i%n]+n+i);
 return 0;
}
void put32(size_t off,uint32_t value){for(int i=0;i<4;++i)saved[off+i]=value>>(8*i);}
void record(uint64_t high, bool legacy=false){
 saved.assign(273,0);std::memcpy(saved.data(),"TPI1",4);put32(4,NODE);
 for(int i=0;i<8;++i)saved[8+i]=high>>(8*i);
 if(legacy){unsigned char key[16];std::memset(key,0xA5,16);saved[16]=1;
  assert(mbedtls_sha256(key,16,saved.data()+17,0)==0);}
}
uint64_t high(){uint64_t n=0;for(int i=0;i<8;++i)n|=uint64_t(saved[8+i])<<(8*i);return n;}
void fill(chat::ChannelRecord& channel,unsigned char byte,size_t count=16){
 std::memset(channel.key,0,sizeof(channel.key));std::memset(channel.key,byte,count);
}
int main(int argc,char** argv){
 assert(argc==2);std::string mode=argv[1];chat::MeshConfig cfg{};
 unsigned char old_key[16],fresh[16];std::memset(old_key,0xA5,16);std::memset(fresh,0xB6,16);
 uint32_t id=99;
 auto next=[&]{return allocatePacketId(NODE,fresh,16,id);};
 if(mode=="fresh"){
  cfg.channels[7].enabled=false;cfg.channels[7].key_len=16;
  std::memcpy(cfg.channels[7].key,old_key,16);
  assert(initializePacketIds(NODE,cfg));assert(commits==1 && high()==1 && saved[16]==1);
  assert(!allocatePacketId(NODE,old_key,16,id) && id==0);
  assert(next() && id==1 && high()==1025);
  chat::MeshConfig changed{};assert(initializePacketIds(NODE,changed));
  assert(!allocatePacketId(NODE,old_key,16,id) && id==0);
  assert(next() && id==2 && commits==2);
 }else if(mode=="reboot"){
  record(5000,true);assert(initializePacketIds(NODE,cfg));
  assert(set_calls==0 && commits==0);
  assert(!allocatePacketId(NODE,old_key,16,id) && id==0);
  assert(next() && id==5000 && high()==6024 && set_calls==1 && commits==1);
 }else if(mode=="last"){
  record(END-1);assert(initializePacketIds(NODE,cfg));
  assert(next() && id==UINT32_MAX && high()==END);
  assert(!next() && id==0);assert(commits==1);
 }else if(mode=="threads"){
  record(1);assert(initializePacketIds(NODE,cfg));std::vector<uint32_t> ids;std::mutex m;
  std::vector<std::thread> threads;
  for(int t=0;t<4;++t)threads.emplace_back([&]{for(int i=0;i<1000;++i){uint32_t n=0;
   assert(allocatePacketId(NODE,fresh,16,n));std::lock_guard<std::mutex> lock(m);ids.push_back(n);}});
  for(auto& t:threads)t.join();
  std::sort(ids.begin(),ids.end());
  assert(ids.size()==4000 && ids.front()==1 && ids.back()==4000);
  assert(std::adjacent_find(ids.begin(),ids.end())==ids.end());assert(commits==4 && high()==4097);
 }else if(mode=="before_init"){
  assert(!next() && id==0);
 }else if(mode=="node_zero"){
  assert(!initializePacketIds(0,cfg));assert(!allocatePacketId(0,nullptr,0,id) && id==0);
 }else if(mode=="wrong_sender"){
  record(5000);assert(!initializePacketIds(NODE+1,cfg));assert(!next() && id==0);
  assert(set_calls==0 && commits==0);
 }else if(mode=="bad_size" || mode=="bad_magic" || mode=="bad_count" || mode=="bad_unused" || mode=="zero" || mode=="overflow"){
  record(mode=="zero"?0:mode=="overflow"?END+1:1);
  if(mode=="bad_size")saved.pop_back();
  if(mode=="bad_magic")saved[0]='X';
  if(mode=="bad_count")saved[16]=9;
  if(mode=="bad_unused")saved.back()=1;
  assert(!initializePacketIds(NODE,cfg));assert(!next() && id==0);
 }else if(mode=="init_open" || mode=="init_read" || mode=="init_read_second" || mode=="init_set" || mode=="init_commit" || mode=="init_sha"){
  if(mode=="init_read" || mode=="init_read_second"){record(5000);fail_get_call=mode=="init_read"?1:2;}
  if(mode=="init_open")fail_open=true;
  if(mode=="init_set")fail_set=true;
  if(mode=="init_commit")fail_commit=true;
  if(mode=="init_sha"){cfg.channels[0].key_len=16;fill(cfg.channels[0],0xA5);fail_sha=true;}
  assert(!initializePacketIds(NODE,cfg));
  if(mode=="init_read" || mode=="init_read_second")assert(set_calls==0 && commits==0);
  int writes=set_calls, prior_commits=commits;
  fail_open=fail_set=fail_commit=fail_sha=false;fail_get_call=0;
  assert(!initializePacketIds(NODE,cfg));assert(!next() && id==0);
  assert(set_calls==writes && commits==prior_commits);
 }else if(mode=="reserve_open" || mode=="reserve_set" || mode=="reserve_commit" || mode=="allocate_sha"){
  record(1,true);assert(initializePacketIds(NODE,cfg));
  fail_open=mode=="reserve_open";fail_set=mode=="reserve_set";
  fail_commit=mode=="reserve_commit";fail_sha=mode=="allocate_sha";
  assert(!next() && id==0);
  int writes=set_calls, prior_commits=commits;
  fail_open=fail_set=fail_commit=fail_sha=false;assert(!next() && id==0);
  assert(set_calls==writes && commits==prior_commits);
 }else if(mode=="invalid_key"){
  record(1);assert(initializePacketIds(NODE,cfg));
  assert(!allocatePacketId(NODE,nullptr,16,id) && id==0);
  assert(!allocatePacketId(NODE,fresh,17,id) && id==0);
 }else if(mode=="public_psks"){
  cfg.channels[0].key_len=16;
  std::memcpy(cfg.channels[0].key,chat::meshtastic::kDefaultPskBytes,16);
  cfg.channels[1].key_len=16;
  std::memcpy(cfg.channels[1].key,chat::meshtastic::kDefaultPskBytes,16);
  cfg.channels[1].key[15]=0xE7;
  assert(initializePacketIds(NODE,cfg) && saved[16]==0);
  assert(allocatePacketId(NODE,cfg.channels[0].key,16,id) && id==1);
  assert(allocatePacketId(NODE,cfg.channels[1].key,16,id) && id==2);
 }else if(mode=="normalization"){
  cfg.channels[0].key_len=0;fill(cfg.channels[0],0x31,16);
  cfg.channels[1].key_len=7;fill(cfg.channels[1],0x42,16);
  cfg.channels[2].key_len=16;fill(cfg.channels[2],0x53,32);
  assert(initializePacketIds(NODE,cfg) && saved[16]==3);
  unsigned char key[32];std::memset(key,0x31,16);
  assert(!allocatePacketId(NODE,key,16,id) && id==0);
  std::memset(key,0x42,16);assert(!allocatePacketId(NODE,key,16,id) && id==0);
  std::memset(key,0x53,16);assert(!allocatePacketId(NODE,key,16,id) && id==0);
  std::memset(key,0x53,32);assert(allocatePacketId(NODE,key,32,id) && id==1);
 }else if(mode=="legacy_32"){
  cfg.channels[4].key_len=32;fill(cfg.channels[4],0x64,32);
  assert(initializePacketIds(NODE,cfg) && saved[16]==1);
  assert(!allocatePacketId(NODE,cfg.channels[4].key,32,id) && id==0);
 }else if(mode=="zero_and_open"){
  cfg.channels[3].key_len=32;
  assert(initializePacketIds(NODE,cfg) && saved[16]==0);
  assert(allocatePacketId(NODE,nullptr,0,id) && id==1);
 }else if(mode=="exhausted"){
  record(END);assert(initializePacketIds(NODE,cfg));assert(!next() && id==0 && commits==0);
 }else{assert(false);}
 assert(opens==closes);
 std::cout<<mode<<" PASS\n";
}
"""

with tempfile.TemporaryDirectory() as directory:
    temp = Path(directory)
    (temp / "mbedtls").mkdir()
    (temp / "nvs.h").write_text(NVS)
    (temp / "mbedtls/sha256.h").write_text(SHA)
    (temp / "test.cpp").write_text(TEST)
    subprocess.run([
        "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fuse-ld=bfd", "-pthread",
        "-I" + str(temp), "-I" + str(ROOT),
        "-I" + str(ROOT / "modules/core_chat/include"),
        str(temp / "test.cpp"), str(ROOT / "platform/esp/radio/persistent_packet_ids.cpp"),
        str(ROOT / "modules/core_chat/src/domain/channel_hash.cpp"),
        "-o", str(temp / "test"),
    ], check=True)
    for scenario in (
        "fresh", "reboot", "last", "threads", "before_init", "node_zero", "wrong_sender",
        "bad_size", "bad_magic", "bad_count", "bad_unused", "zero", "overflow",
        "init_open", "init_read", "init_read_second", "init_set", "init_commit", "init_sha",
        "reserve_open", "reserve_set", "reserve_commit", "allocate_sha", "invalid_key",
        "public_psks", "normalization", "legacy_32", "zero_and_open", "exhausted",
    ):
        subprocess.run([str(temp / "test"), scenario], check=True)
