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
#include "nvs.h"
#include "mbedtls/sha256.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
using namespace platform::esp::radio;
std::vector<unsigned char> saved, pending;
bool fail_open=false, fail_get=false, fail_set=false, fail_commit=false, fail_sha=false;
int commits=0;
constexpr uint32_t NODE=0x12345678;
constexpr uint64_t END=1ULL<<32;
int nvs_open(const char* ns,int mode,nvs_handle_t* h){
 assert(std::strcmp(ns,"tm_pkt_ids")==0 && mode==NVS_READWRITE);
 *h=7;return fail_open?-1:ESP_OK;
}
int nvs_get_blob(nvs_handle_t h,const char* k,void* out,size_t* n){
 assert(h==7 && std::strcmp(k,"state_v1")==0);
 if(fail_get)return -1;
 if(saved.empty())return ESP_ERR_NVS_NOT_FOUND;
 if(out){if(*n<saved.size())return -1;std::memcpy(out,saved.data(),saved.size());}
 *n=saved.size();return ESP_OK;
}
int nvs_set_blob(nvs_handle_t h,const char* k,const void* p,size_t n){
 assert(h==7 && std::strcmp(k,"state_v1")==0 && n==273);
 if(fail_set)return -1;
 auto bytes=static_cast<const unsigned char*>(p);pending.assign(bytes,bytes+n);return ESP_OK;
}
int nvs_commit(nvs_handle_t h){
 assert(h==7);saved=pending;++commits;return fail_commit?-1:ESP_OK;
}
void nvs_close(nvs_handle_t h){assert(h==7);}
int mbedtls_sha256(const unsigned char* p,size_t n,unsigned char* out,int is224){
 assert(p && (n==16 || n==32) && is224==0);
 if(fail_sha)return -1;
 // Observable deterministic substitute, NOT a cryptographic implementation.
 for(size_t i=0;i<32;++i)out[i]=static_cast<unsigned char>(p[i%n]+n+i);
 return 0;
}
void record(uint64_t high, bool legacy=false){
 saved.assign(273,0);std::memcpy(saved.data(),"TPI1",4);
 for(int i=0;i<4;++i)saved[4+i]=NODE>>(8*i);
 for(int i=0;i<8;++i)saved[8+i]=high>>(8*i);
 if(legacy){unsigned char key[16];std::memset(key,0xA5,16);saved[16]=1;
  assert(mbedtls_sha256(key,16,saved.data()+17,0)==0);}
}
uint64_t high(){uint64_t n=0;for(int i=0;i<8;++i)n|=uint64_t(saved[8+i])<<(8*i);return n;}
int main(int argc,char** argv){
 assert(argc==2);std::string mode=argv[1];chat::MeshConfig cfg{};
 unsigned char old_key[16],fresh[16];std::memset(old_key,0xA5,16);std::memset(fresh,0xB6,16);
 uint32_t id=99;
 auto next=[&](uint32_t request=0){return allocatePacketId(NODE,fresh,16,request,id);};
 if(mode=="fresh"){
  cfg.channels[7].enabled=false;cfg.channels[7].key_len=16;
  std::memcpy(cfg.channels[7].key,old_key,16);
  assert(initializePacketIds(NODE,cfg));assert(commits==1 && high()==1 && saved[16]==1);
  assert(!allocatePacketId(NODE,old_key,16,0,id) && id==0);
  assert(next() && id==1 && high()==1025);
  chat::MeshConfig changed{};assert(initializePacketIds(NODE,changed));
  assert(!allocatePacketId(NODE,old_key,16,0,id) && id==0);
  assert(next() && id==2 && commits==2);
 }else if(mode=="reboot"){
  record(1025,true);assert(initializePacketIds(NODE,cfg));
  assert(!allocatePacketId(NODE,old_key,16,0,id) && id==0);
  assert(next() && id==1025 && high()==2049);
  assert(!next(1025) && id==0);assert(next(5000) && id==5000 && high()>5000);
 }else if(mode=="last"){
  record(END-1);assert(initializePacketIds(NODE,cfg));
  assert(next() && id==UINT32_MAX && high()==END);
  assert(!next() && id==0);assert(!next(UINT32_MAX) && id==0);
 }else if(mode=="threads"){
  record(1);assert(initializePacketIds(NODE,cfg));std::vector<uint32_t> ids;std::mutex m;
  std::vector<std::thread> threads;
  for(int t=0;t<4;++t)threads.emplace_back([&]{for(int i=0;i<1000;++i){uint32_t n=0;
   assert(allocatePacketId(NODE,fresh,16,0,n));std::lock_guard<std::mutex> lock(m);ids.push_back(n);}});
  for(auto& t:threads)t.join();std::sort(ids.begin(),ids.end());
  assert(ids.size()==4000 && ids.front()==1 && ids.back()==4000);
  assert(std::adjacent_find(ids.begin(),ids.end())==ids.end());assert(commits==4 && high()==4097);
 }else if(mode=="before_init"){
  assert(!next() && id==0);
 }else if(mode=="wrong_sender"){
  record(1);assert(!initializePacketIds(NODE+1,cfg));assert(!next() && id==0);
 }else if(mode=="bad_size" || mode=="bad_magic" || mode=="bad_count" || mode=="bad_unused" || mode=="zero" || mode=="overflow"){
  record(mode=="zero"?0:mode=="overflow"?END+1:1);
  if(mode=="bad_size")saved.pop_back();if(mode=="bad_magic")saved[0]='X';
  if(mode=="bad_count")saved[16]=9;if(mode=="bad_unused")saved.back()=1;
  assert(!initializePacketIds(NODE,cfg));assert(!next() && id==0);
 }else if(mode=="init_open" || mode=="init_read" || mode=="init_set" || mode=="init_commit" || mode=="init_sha"){
  if(mode=="init_read"){record(1);fail_get=true;}
  if(mode=="init_open")fail_open=true;if(mode=="init_set")fail_set=true;
  if(mode=="init_commit")fail_commit=true;
  if(mode=="init_sha"){cfg.channels[0].key_len=16;cfg.channels[0].key[0]=0xA5;fail_sha=true;}
  assert(!initializePacketIds(NODE,cfg));
  fail_open=fail_get=fail_set=fail_commit=fail_sha=false;
  assert(!initializePacketIds(NODE,cfg));assert(!next() && id==0);
 }else if(mode=="reserve_open" || mode=="reserve_set" || mode=="reserve_commit" || mode=="allocate_sha"){
  record(1,true);assert(initializePacketIds(NODE,cfg));
  fail_open=mode=="reserve_open";fail_set=mode=="reserve_set";
  fail_commit=mode=="reserve_commit";fail_sha=mode=="allocate_sha";
  assert(!next() && id==0);
  fail_open=fail_set=fail_commit=fail_sha=false;assert(!next() && id==0);
 }else if(mode=="invalid_key"){
  record(1);assert(initializePacketIds(NODE,cfg));
  assert(!allocatePacketId(NODE,nullptr,16,0,id) && id==0);
  assert(!allocatePacketId(NODE,fresh,17,0,id) && id==0);
 }else if(mode=="exhausted"){
  record(END);assert(initializePacketIds(NODE,cfg));assert(!next() && id==0 && commits==0);
 }else{assert(false);}
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
        "g++", "-std=c++17", "-Wall", "-Wextra", "-fuse-ld=bfd", "-pthread",
        "-I" + str(temp), "-I" + str(ROOT),
        "-I" + str(ROOT / "modules/core_chat/include"),
        str(temp / "test.cpp"), str(ROOT / "platform/esp/radio/persistent_packet_ids.cpp"),
        "-o", str(temp / "test"),
    ], check=True)
    for scenario in (
        "fresh", "reboot", "last", "threads", "before_init", "wrong_sender",
        "bad_size", "bad_magic", "bad_count", "bad_unused", "zero", "overflow",
        "init_open", "init_read", "init_set", "init_commit", "init_sha",
        "reserve_open", "reserve_set", "reserve_commit", "allocate_sha",
        "invalid_key", "exhausted",
    ):
        subprocess.run([str(temp / "test"), scenario], check=True)
