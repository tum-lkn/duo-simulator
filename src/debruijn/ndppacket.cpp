#include "ndppacket.h"
#include "ndp.h"

PacketDB<NdpPacket> NdpPacket::_packetdb;
PacketDB<NdpAck> NdpAck::_packetdb;
PacketDB<NdpNack> NdpNack::_packetdb;
PacketDB<NdpPull> NdpPull::_packetdb;

bool NdpPacket::is_vlb() const {
    return _ndpsrc->_vlb;
}

PacketSink *NdpPacket::get_flow_sink() { return _ndpsink; }

PacketSink *NdpPacket::get_flow_src() { return _ndpsrc; }

PacketSink *NdpAck::get_flow_src() { return _ndpsrc; }

PacketSink *NdpNack::get_flow_src() { return _ndpsrc; }

PacketSink *NdpPull::get_flow_src() { return _ndpsrc; }

void NdpPacket::strip_payload() {
    // +1 since we start with -1; 0 means drop at host queue
    int idx = _crthop+1;
    if (idx >= _ndpsrc->_stripped_at_hop.size()) {
        cout << "Packet with " << _crthop << ">9 hops " << _src << "->" << _dst << " at " << _crtToR << endl;
        idx =  _ndpsrc->_stripped_at_hop.size() - 1;
    }
    _ndpsrc->_stripped_at_hop.at(idx)++;
    Packet::strip_payload();
    _size = ACKSIZE;
};