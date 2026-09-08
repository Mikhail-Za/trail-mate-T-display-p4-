#!/usr/bin/env python3
"""Run actual native send bodies and wire codec; observe IDs and AES nonce inputs."""
from pathlib import Path
import resource
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
SOURCE = (ROOT / "platform/esp/radio/meshtastic_radio_adapter.cpp").read_text()
HEADER = (ROOT / "platform/esp/radio/meshtastic_radio_adapter.h").read_text()


def function(name):
    start = SOURCE.index(name + "(")
    start = SOURCE.rfind("\n", 0, start) + 1
    opening = SOURCE.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (SOURCE[end] == "{") - (SOURCE[end] == "}")
        end += 1
    return SOURCE[start:end]


start = HEADER.index("bool sendEncodedPayload(")
declaration = HEADER[start:HEADER.index(";", start) + 1]
AES = r"""
#pragma once
#include <array>
#include <vector>
#include <cstring>
extern std::vector<std::array<unsigned char,16>> nonces;
struct mbedtls_aes_context {};
inline void mbedtls_aes_init(mbedtls_aes_context*){}
inline void mbedtls_aes_free(mbedtls_aes_context*){}
inline int mbedtls_aes_setkey_enc(mbedtls_aes_context*,const unsigned char*,unsigned){return 0;}
inline int mbedtls_aes_crypt_ctr(mbedtls_aes_context*,size_t n,size_t*,unsigned char* nonce,
 unsigned char*,const unsigned char* in,unsigned char* out){
 std::array<unsigned char,16> value{};std::memcpy(value.data(),nonce,16);nonces.push_back(value);
 std::memmove(out,in,n);return 0;
}
"""
STUBS = r"""
#include "chat/domain/chat_types.h"
#include "chat/domain/persistent_packet_id_allocator.h"
#include "chat/infra/meshtastic/mt_packet_wire.h"
#include <array>
#include <vector>
#include <cassert>
#include <cstring>
#include <iostream>
std::vector<std::array<unsigned char,16>> nonces;
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
namespace sys {
struct ChatSendResultEvent{ChatSendResultEvent(uint32_t,bool){}};
struct EventBus{static void publish(ChatSendResultEvent* p,int){delete p;}};
}
namespace chat::meshtastic {
bool encodeTextMessage(ChannelId,const std::string& text,NodeId,MessageId,NodeId,uint8_t* p,size_t* n){
 if(text.size()>*n)return false;std::memcpy(p,text.data(),text.size());*n=text.size();return true;
}
bool encodeAppData(uint32_t,const uint8_t* data,size_t len,bool,uint8_t* p,size_t* n){
 if(len>*n)return false;std::memcpy(p,data,len);*n=len;return true;
}
}
namespace platform::esp::radio {
constexpr uint32_t kRadioOk=0;
int allocations=0,initializations=0;bool initialized=false,init_allowed=true,allocate_allowed=true;
chat::PersistentPacketIdAllocator allocator(1);
bool initializePacketIds(uint32_t,const chat::MeshConfig&){++initializations;initialized=init_allowed;return initialized;}
bool allocatePacketId(uint32_t,const uint8_t*,size_t,uint32_t requested,uint32_t& out){
 ++allocations;out=0;if(!initialized || !allocate_allowed)return false;
 return allocator.allocate(requested,[](uint64_t){return true;},out);
}
struct Board{
 bool online=true,success=true;std::vector<uint32_t> attempts;
 bool isRadioOnline() const{return online;}
 int transmitRadio(const uint8_t* data,size_t len){
  assert(len>sizeof(chat::meshtastic::PacketHeaderWire));chat::meshtastic::PacketHeaderWire h;
  std::memcpy(&h,data,sizeof(h));assert(h.from==0x12345678);attempts.push_back(h.id);return success?0:-1;
 }
};
class MeshtasticRadioAdapter {
public:
 Board board_;bool ready_=false,rx_started_=false;
 chat::NodeId node_id_=0x12345678;
 chat::MessageId next_packet_id_=1; // Baseline-only member: test must fail before repair.
 static constexpr uint32_t kBroadcastNodeId=0xFFFFFFFF;
 chat::MeshConfig config_{};bool channel_enabled_[chat::kMaxChannels]{};
 uint8_t key_[16]{1};
 bool sendText(chat::ChannelId,const std::string&,chat::MessageId*,chat::NodeId=0);
 bool sendAppData(chat::ChannelId,uint32_t,const uint8_t*,size_t,chat::NodeId=0,bool=false,chat::MessageId=0,bool=false);
 void applyConfig(const chat::MeshConfig&);
 DECLARATION
 const uint8_t* channelKeyFor(chat::ChannelId,size_t* n) const{*n=16;return key_;}
 uint8_t channelHashFor(chat::ChannelId) const{return 5;}
 void updateChannelKeys(){for(size_t i=0;i<chat::kMaxChannels;++i)channel_enabled_[i]=config_.channels[i].enabled;}
 void configureRadio(){ready_=true;}
 void ensureReceiveStarted(){rx_started_=true;}
};
BODY
}
int main(){
 using namespace platform::esp::radio;
 MeshtasticRadioAdapter a;chat::MeshConfig cfg{};cfg.channels[0].enabled=true;a.applyConfig(cfg);
 uint32_t id=999;assert(a.sendText(chat::ChannelId::PRIMARY,"hello",&id));
 assert(initializations==1 && allocations==1 && id==1 && a.board_.attempts.back()==id);
 unsigned char payload[]={1,2,3};
 assert(a.sendAppData(chat::ChannelId::PRIMARY,1,payload,3));assert(a.board_.attempts.back()==2);
 a.board_.success=false;assert(!a.sendText(chat::ChannelId::PRIMARY,"fail",&id));
 assert(id==3 && a.board_.attempts.back()==3);a.board_.success=true;
 assert(a.sendText(chat::ChannelId::PRIMARY,"next",&id) && id==4);
 assert(a.sendAppData(chat::ChannelId::PRIMARY,1,payload,3,0,false,100));
 const size_t sent=a.board_.attempts.size();const size_t encrypted=nonces.size();
 assert(!a.sendAppData(chat::ChannelId::PRIMARY,1,payload,3,0,false,100));
 assert(a.board_.attempts.size()==sent && nonces.size()==encrypted);
 allocate_allowed=false;id=999;assert(!a.sendText(chat::ChannelId::PRIMARY,"blocked",&id));
 assert(id==0 && a.board_.attempts.size()==sent && nonces.size()==encrypted);allocate_allowed=true;
 const int before=allocations;
 assert(!a.sendText(chat::ChannelId::SECONDARY,"disabled",&id) && id==0);
 assert(!a.sendText(static_cast<chat::ChannelId>(255),"invalid",&id) && id==0);
 assert(allocations==before && a.board_.attempts.size()==sent);
 a.applyConfig(cfg);assert(a.sendText(chat::ChannelId::PRIMARY,"still",&id) && id==101);
 MeshtasticRadioAdapter b;b.applyConfig(cfg);assert(b.sendText(chat::ChannelId::PRIMARY,"second",&id) && id==102);
 assert(nonces.size()==a.board_.attempts.size()+b.board_.attempts.size());
 for(size_t i=0;i<nonces.size();++i){
  uint64_t packet=0;uint32_t sender=0;std::memcpy(&packet,nonces[i].data(),8);std::memcpy(&sender,nonces[i].data()+8,4);
  assert(sender==0x12345678 && packet>0 && packet<=UINT32_MAX);
  assert(nonces[i][12]==0 && nonces[i][13]==0 && nonces[i][14]==0 && nonces[i][15]==0);
  for(size_t j=0;j<i;++j)assert(nonces[i]!=nonces[j]);
 }
 init_allowed=false;a.applyConfig(cfg);id=999;
 assert(!a.sendText(chat::ChannelId::PRIMARY,"init failed",&id) && id==0);
 std::cout<<"native send IDs and wire nonce inputs PASS\n";
}
"""
body = "\n".join(function("MeshtasticRadioAdapter::" + name) for name in (
    "sendText", "sendAppData", "applyConfig", "sendEncodedPayload",
))
with tempfile.TemporaryDirectory() as directory:
    temp = Path(directory)
    (temp / "mbedtls").mkdir()
    (temp / "mbedtls/aes.h").write_text(AES)
    (temp / "test.cpp").write_text(STUBS.replace("DECLARATION", declaration).replace("BODY", body))
    subprocess.run([
        "g++", "-std=c++17", "-Wall", "-Wextra", "-fuse-ld=bfd", "-DESP_PLATFORM",
        "-I" + str(temp), "-I" + str(ROOT / "modules/core_chat/include"),
        str(temp / "test.cpp"), str(ROOT / "modules/core_chat/src/infra/meshtastic/mt_packet_wire.cpp"),
        "-o", str(temp / "test"),
    ], check=True)
    subprocess.run([str(temp / "test")], check=True)
assert "next_packet_id_" not in HEADER and "next_packet_id_" not in SOURCE
