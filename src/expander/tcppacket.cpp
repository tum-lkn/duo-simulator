#include "tcppacket.h"
#include "tcp.h"

PacketDB<TcpPacket> TcpPacket::_packetdb;
PacketDB<TcpAck> TcpAck::_packetdb;

void TcpPacket::track_drop() {
    // +1 since we start with -1; 0 means drop at host queue
    int idx = current_hop;
    if (idx >= _tcpsrc->_dropped_at_hop.size()) {
        idx =  _tcpsrc->_dropped_at_hop.size() - 1;
    }
    _tcpsrc->_dropped_at_hop.at(idx)++;
};