"""Owned loopback counterexamples for individual complete NAWS events."""
from __future__ import annotations
from pathlib import Path
import socket
import sys
import tempfile
import time
import unittest

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from research.ui.alternate_style_reference import Peer


def naws(columns,rows):
    return bytes([255,250,31])+columns.to_bytes(2,"big")+rows.to_bytes(2,"big")+bytes([255,240])


class OneBatchConnection:
    """Keep the tested receive boundary deterministic over a real loopback TCP."""
    def __init__(self,connection):
        self.connection=connection
        self.batched=False

    def __getattr__(self,name):
        return getattr(self.connection,name)

    def recv(self,size):
        if self.batched:
            return self.connection.recv(size)
        received=bytearray()
        deadline=time.monotonic()+1
        while len(received)<27 and time.monotonic()<deadline:
            try:
                part=self.connection.recv(27-len(received))
            except socket.timeout:
                continue
            if not part:
                break
            received.extend(part)
        self.batched=True
        return bytes(received)


class OneBatchListener:
    def __init__(self,listener):
        self.listener=listener

    def __getattr__(self,name):
        return getattr(self.listener,name)

    def accept(self):
        connection,address=self.listener.accept()
        return OneBatchConnection(connection),address


class BatchedPeer(Peer):
    def _serve(self):
        self.listener=OneBatchListener(self.listener)
        super()._serve()


@unittest.skipUnless(hasattr(socket,"SO_EXCLUSIVEADDRUSE"),"Original Windows UI peer uses Windows exclusive-bind semantics")
class NawsEventTests(unittest.TestCase):
    def run_peer(self,frozen):
        with tempfile.TemporaryDirectory() as temporary:
            peer=BatchedPeer(Path(temporary))
            if frozen:
                peer.state["frozen_grid"]=[91,25]
            try:
                with socket.create_connection(("127.0.0.1",peer.port),timeout=1) as client:
                    negotiation=bytearray()
                    while len(negotiation)<9:
                        negotiation.extend(client.recv(9-len(negotiation)))
                    self.assertEqual(bytes(negotiation),bytes([255,251,1,255,251,3,255,253,31]))
                    # Exactly three complete NAWS frames in one parser receive,
                    # including a transient change that returns to the old grid.
                    client.sendall(naws(91,25)+naws(92,25)+naws(91,25))
                    deadline=time.monotonic()+1.5
                    while time.monotonic()<deadline:
                        if peer.state.get("error") or (not frozen and len(peer.state.get("naws_history",[]))>=3):
                            break
                        time.sleep(.01)
                    state=dict(peer.state)
            finally:
                peer.close()
            return state

    def test_transient_change_in_same_receive_is_rejected_after_freeze(self):
        state=self.run_peer(frozen=True)
        self.assertEqual(state.get("error"),"NAWS changed after fixture freeze")
        self.assertEqual(state.get("naws_history"),[[91,25],[92,25]])
        self.assertEqual([state["columns"],state["rows"]],[92,25])

    def test_every_complete_event_is_retained_before_freeze(self):
        state=self.run_peer(frozen=False)
        self.assertIsNone(state.get("error"))
        self.assertEqual(state.get("naws_history"),[[91,25],[92,25],[91,25]])
        self.assertEqual([state["columns"],state["rows"]],[91,25])


if __name__=="__main__":
    unittest.main()
