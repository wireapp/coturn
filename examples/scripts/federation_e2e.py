#!/usr/bin/env python3
# Two-server federation driver (see examples/run_tests_federation.sh).
#
# An sft- user allocates on each server and gets the server's federation address
# as its relayed address, plus a CHANNEL-NUMBER that identifies the allocation on
# the federation link. Each client then sends a Send indication to the other
# server's federation address with the peer's channel number in the first two
# bytes of DATA; the other client must receive the DATA payload verbatim.
# A regular user must still get a normal relay without a CHANNEL-NUMBER.
#
# Usage: federation_e2e.py <server-a-port> <server-b-port>

import hashlib
import hmac
import os
import socket
import struct
import sys

MAGIC = 0x2112A442
ATTR_CHANNEL_NUMBER = 0x000C
ATTR_USERNAME = 0x0006
ATTR_MESSAGE_INTEGRITY = 0x0008
ATTR_ERROR_CODE = 0x0009
ATTR_XOR_PEER_ADDRESS = 0x0012
ATTR_DATA = 0x0013
ATTR_REALM = 0x0014
ATTR_NONCE = 0x0015
ATTR_XOR_RELAYED_ADDRESS = 0x0016
ATTR_REQUESTED_TRANSPORT = 0x0019
ALLOCATE = 0x0003
ALLOCATE_SUCCESS = 0x0103
SEND_INDICATION = 0x0016
ROUNDS = 3


def fail(reason):
    print('RESULT FAIL %s' % reason)
    sys.exit(1)


def attr(t, v):
    pad = (4 - len(v) % 4) % 4
    return struct.pack('!HH', t, len(v)) + v + b'\0' * pad


def msg(mtype, attrs, key=None):
    tid = os.urandom(12)
    body = b''.join(attrs)
    if key is not None:
        hdr = struct.pack('!HHI', mtype, len(body) + 24, MAGIC) + tid
        body += attr(ATTR_MESSAGE_INTEGRITY, hmac.new(key, hdr + body, hashlib.sha1).digest())
    return struct.pack('!HHI', mtype, len(body), MAGIC) + tid + body


def parse(data):
    mtype, mlen, _ = struct.unpack('!HHI', data[:8])
    attrs, off = {}, 20
    while off < 20 + mlen:
        t, l = struct.unpack('!HH', data[off:off + 4])
        attrs[t] = data[off + 4:off + 4 + l]
        off += 4 + l + (4 - l % 4) % 4
    return mtype, attrs


def xor_addr(ip, port):
    xip = bytes(a ^ b for a, b in zip(socket.inet_aton(ip), struct.pack('!I', MAGIC)))
    return struct.pack('!BBH', 0, 1, port ^ (MAGIC >> 16)) + xip


def unxor_addr(v):
    port = struct.unpack('!H', v[2:4])[0] ^ (MAGIC >> 16)
    ip = socket.inet_ntoa(bytes(x ^ y for x, y in zip(v[4:8], struct.pack('!I', MAGIC))))
    return ip, port


def allocate(sock, server, user, pwd):
    udp = attr(ATTR_REQUESTED_TRANSPORT, b'\x11\0\0\0')
    sock.sendto(msg(ALLOCATE, [udp]), server)
    _, a = parse(sock.recv(2048))
    realm, nonce = a[ATTR_REALM], a[ATTR_NONCE]
    key = hashlib.md5(user.encode() + b':' + realm + b':' + pwd.encode()).digest()
    auth = [udp, attr(ATTR_USERNAME, user.encode()), attr(ATTR_REALM, realm), attr(ATTR_NONCE, nonce)]
    sock.sendto(msg(ALLOCATE, auth, key), server)
    mtype, a = parse(sock.recv(2048))
    if mtype != ALLOCATE_SUCCESS:
        fail('allocate %s on %s: 0x%04x %r' % (user, server, mtype, a.get(ATTR_ERROR_CODE)))
    cid = None
    if ATTR_CHANNEL_NUMBER in a:
        cid = struct.unpack('!H', a[ATTR_CHANNEL_NUMBER][:2])[0]
    return unxor_addr(a[ATTR_XOR_RELAYED_ADDRESS]), cid


def udp_socket():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(5)
    return s


def main():
    a_srv, b_srv = ('127.0.0.1', int(sys.argv[1])), ('127.0.0.1', int(sys.argv[2]))
    sa, sb = udp_socket(), udp_socket()
    try:
        a_fed, a_cid = allocate(sa, a_srv, 'sft-a', 'pwd')
        b_fed, b_cid = allocate(sb, b_srv, 'sft-b', 'pwd')
    except socket.timeout:
        fail('no allocate response')
    if a_cid is None or b_cid is None:
        fail('sft- allocation has no CHANNEL-NUMBER')
    print('A federation=%s cid=0x%04x, B federation=%s cid=0x%04x' % (a_fed, a_cid, b_fed, b_cid))

    for rnd in range(ROUNDS):
        for src, srv, dst_fed, dst_cid, rcv, name in ((sa, a_srv, b_fed, b_cid, sb, 'A->B'),
                                                      (sb, b_srv, a_fed, a_cid, sa, 'B->A')):
            payload = struct.pack('!HH', dst_cid, 8) + b'msg-%04d' % rnd
            src.sendto(msg(SEND_INDICATION, [attr(ATTR_XOR_PEER_ADDRESS, xor_addr(*dst_fed)),
                                             attr(ATTR_DATA, payload)]), srv)
            try:
                got = rcv.recv(2048)
            except socket.timeout:
                fail('%s round %d: nothing received' % (name, rnd))
            if got != payload:
                fail('%s round %d: got %r, want %r' % (name, rnd, got, payload))
            print('%s round %d ok' % (name, rnd))

    try:
        relayed, cid = allocate(udp_socket(), a_srv, 'user', 'pwd')
    except socket.timeout:
        fail('no allocate response for regular user')
    if cid is not None or relayed == a_fed:
        fail('regular user got a federation allocation: relayed=%s cid=%r' % (relayed, cid))
    print('RESULT OK')


main()
