#include "tcppacket.h"
#include "tcp.h"

PacketDB<TcpPacket> TcpPacket::_packetdb;
PacketDB<TcpAck> TcpAck::_packetdb;

PacketSink *TcpPacket::get_flow_src() { return _tcpsrc; }

PacketSink *TcpPacket::get_flow_sink() { return _tcpsink; }

PacketSink *TcpAck::get_flow_src() { return _tcpsrc; }

void TcpPacket::track_drop() {
    // +1 since we start with -1; 0 means drop at host queue
    int idx = _crthop+1;
    if (idx >= _tcpsrc->_dropped_at_hop.size()) {
        cout << "Packet with " << _crthop << ">9 hops " << _src << "->" << _dst << " at " << _crtToR << endl;
        idx =  _tcpsrc->_dropped_at_hop.size() - 1;
    }
    _tcpsrc->_dropped_at_hop.at(idx)++;
};