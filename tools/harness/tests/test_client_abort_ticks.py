"""A network client's game left running as a local one when its host went down: its local ticks touch none of the
host's queues, which a client never started (the Steam Deck's crash in update_server_handle_client_update)."""
import sys
from pathlib import Path
import pytest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED,build,function,mutated,read,run

def generated(fault=None):
    code=function(read('source/game/player_queues_new.c'),'update_client_local_ticks')
    if fault:code=mutated(code,*fault)
    return (('under_test.inc',code),)

@pytest.mark.parametrize('case',['local','client-aborted','uninitialized'])
def test_local_ticks(case):
    status,output=run(build('client_abort_ticks',generated()),case)
    assert status==0,output

def test_negative_control():
    fault=('if (!update_server_globals.initialized || !update_server_globals.queues->valid)\n\t\treturn;','')
    status,output=run(build('client_abort_ticks',generated(fault)),'client-aborted')
    assert status==CHECK_FAILED,output
