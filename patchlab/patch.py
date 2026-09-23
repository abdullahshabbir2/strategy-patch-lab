import hashlib
import json
from pathlib import Path
import struct
import math
from .pe import PEImage

ORIGINAL_GATE=bytes.fromhex('b80000175ac3')


def sha(data): return hashlib.sha256(data).hexdigest()


def inspect(data):
    pe=PEImage(data)
    section=pe.section('.patch')
    if not section['flags']&0x20000000: raise ValueError('patch section must be executable')
    start=section['raw_offset']
    size=min(section['raw_size'],section['virtual_size'])
    offsets=[]
    block=data[start:start+size]
    for i in range(max(0,len(block)-5)):
        if block[i]==0xb8 and block[i+3:i+6]==bytes.fromhex('175ac3'):
            offsets.append(start+i)
    if len(offsets)!=1: raise ValueError('unique owned feature-gate instruction required')
    offset=offsets[0]
    marker=int.from_bytes(data[offset+1:offset+5],'little')
    if marker&0xffff0000!=0x5a170000 or marker&0xffff>15: raise ValueError('invalid feature marker')
    calibration=pe.section('.cal')
    if min(calibration['raw_size'],calibration['virtual_size'])<132: raise ValueError('calibration section truncated')
    values=struct.unpack_from('<33f',data,calibration['raw_offset'])
    if not all(math.isfinite(v) for v in values): raise ValueError('nonfinite calibration data')
    if list(values[:3])!=[1000,3000,6000] or list(values[3:6])!=[0,50,100]:
        raise ValueError('unexpected owned calibration axes')
    return dict(format='PE32+',machine='AMD64 host model',image_base=pe.image_base,
                sha256=sha(data),size=len(data),sections=pe.sections,
                gate=dict(file_offset=offset,rva=section['rva']+offset-start,
                          virtual_address=pe.image_base+section['rva']+offset-start,
                          bytes=data[offset:offset+6].hex(),features=marker&0xffff),
                checksum=dict(offset=pe.checksum_offset,stored=pe.stored_checksum,calculated=pe.checksum()),
                calibration=dict(file_offset=calibration['raw_offset'],rva=calibration['rva'],
                                 encoding='33 little-endian IEEE-754 floats; rpm axis, pedal axis, three row-major maps',
                                 rpm_axis=list(values[:3]),pedal_axis=list(values[3:6]),
                                 torque_maps=[list(values[6+i*9:15+i*9]) for i in range(3)]))


def make_manifest(data,features=15):
    if type(features) is not int or not 1<=features<=15: raise ValueError('feature mask must be 1..15')
    info=inspect(data)
    if bytes.fromhex(info['gate']['bytes'])!=ORIGINAL_GATE: raise ValueError('baseline feature gate not found')
    if info['checksum']['stored']!=info['checksum']['calculated']: raise ValueError('baseline PE checksum invalid')
    return {'schema_version':1,'baseline_sha256':sha(data),'file_offset':info['gate']['file_offset'],
            'expected_bytes':ORIGINAL_GATE.hex(),'replacement_bytes':(b'\xb8'+(0x5a170000|features).to_bytes(4,'little')+b'\xc3').hex()}


def apply(data,manifest):
    if not isinstance(manifest,dict) or set(manifest)!={'schema_version','baseline_sha256','file_offset','expected_bytes','replacement_bytes'}:
        raise ValueError('invalid patch manifest fields')
    if type(manifest['schema_version']) is not int or manifest['schema_version']!=1 or manifest['baseline_sha256']!=sha(data):
        raise ValueError('baseline hash/version precondition failed')
    info=inspect(data)
    offset=manifest['file_offset']
    if type(offset) is not int or offset!=info['gate']['file_offset']: raise ValueError('patch location differs from analyzed gate')
    expected=bytes.fromhex(manifest['expected_bytes'])
    replacement=bytes.fromhex(manifest['replacement_bytes'])
    if expected!=ORIGINAL_GATE or data[offset:offset+6]!=expected: raise ValueError('instruction precondition failed')
    if len(replacement)!=6 or replacement[0]!=0xb8 or replacement[2:]!=ORIGINAL_GATE[2:] or not 1<=replacement[1]<=15:
        raise ValueError('replacement outside allowed feature-mask patch')
    if info['checksum']['stored']!=info['checksum']['calculated']: raise ValueError('baseline PE checksum invalid')
    output=bytearray(data)
    output[offset:offset+6]=replacement
    checksum_offset=info['checksum']['offset']
    struct.pack_into('<I',output,checksum_offset,PEImage(output).checksum())
    allowed=set(range(offset,offset+6))|set(range(checksum_offset,checksum_offset+4))
    if any(a!=b and i not in allowed for i,(a,b) in enumerate(zip(data,output))):
        raise AssertionError('protected bytes modified')
    receipt={'schema_version':1,'baseline_sha256':sha(data),'candidate_sha256':sha(output),
             'file_offset':offset,'original_bytes':expected.hex(),'candidate_bytes':replacement.hex(),
             'checksum_offset':checksum_offset,'original_checksum_bytes':data[checksum_offset:checksum_offset+4].hex(),
             'changed_offsets':[i for i,(a,b) in enumerate(zip(data,output)) if a!=b],
             'protected_bytes_identical':True,'analysis':inspect(output)}
    return bytes(output),receipt


def rollback(data,receipt):
    if receipt.get('schema_version')!=1 or receipt.get('candidate_sha256')!=sha(data):
        raise ValueError('rollback candidate precondition failed')
    info=inspect(data)
    offset,checksum=receipt.get('file_offset'),receipt.get('checksum_offset')
    if offset!=info['gate']['file_offset'] or checksum!=info['checksum']['offset']:
        raise ValueError('rollback location mismatch')
    original=bytes.fromhex(receipt['original_bytes'])
    original_checksum=bytes.fromhex(receipt['original_checksum_bytes'])
    if original!=ORIGINAL_GATE or len(original_checksum)!=4: raise ValueError('invalid rollback bytes')
    output=bytearray(data)
    output[offset:offset+6]=original
    output[checksum:checksum+4]=original_checksum
    if sha(output)!=receipt['baseline_sha256']: raise ValueError('rollback baseline hash mismatch')
    return bytes(output)


def write_new(path,data):
    # Exclusive creation prevents accidental overwrite of an existing binary.
    with Path(path).open('xb') as stream:
        stream.write(data)
