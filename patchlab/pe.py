import struct


class PEImage:
    def __init__(self,data):
        self.data=data
        if not 256 <= len(data) <= 64*1024*1024 or data[:2]!=b'MZ':
            raise ValueError('invalid or oversized DOS image')
        pe=struct.unpack_from('<I',data,0x3c)[0]
        if pe+24>len(data) or data[pe:pe+4]!=b'PE\0\0': raise ValueError('invalid PE header')
        machine,count=struct.unpack_from('<HH',data,pe+4)
        optional_size=struct.unpack_from('<H',data,pe+20)[0]
        optional=pe+24
        if machine!=0x8664 or not 1<=count<=96 or optional_size<152 or optional+optional_size>len(data):
            raise ValueError('only bounded AMD64 PE32+ images supported')
        if struct.unpack_from('<H',data,optional)[0]!=0x20b: raise ValueError('PE32+ required')
        self.image_base=struct.unpack_from('<Q',data,optional+24)[0]
        self.entry_rva=struct.unpack_from('<I',data,optional+16)[0]
        self.checksum_offset=optional+64
        self.stored_checksum=struct.unpack_from('<I',data,self.checksum_offset)[0]
        directories=struct.unpack_from('<I',data,optional+108)[0]
        if directories<5: raise ValueError('certificate-directory field missing')
        cert_offset,cert_size=struct.unpack_from('<II',data,optional+112+4*8)
        if cert_offset or cert_size: raise ValueError('signed images are outside this lab patch policy')
        table=optional+optional_size
        if table+40*count>len(data): raise ValueError('truncated section table')
        self.sections=[]
        names=set()
        header_end=table+40*count
        for i in range(count):
            offset=table+i*40
            name=data[offset:offset+8].split(b'\0')[0].decode('ascii','strict')
            vsize,rva,size,raw=struct.unpack_from('<IIII',data,offset+8)
            flags=struct.unpack_from('<I',data,offset+36)[0]
            if name in names: raise ValueError('duplicate section name')
            names.add(name)
            if size and (raw<header_end or raw+size>len(data)): raise ValueError('section outside file')
            for section in self.sections:
                if size and section['raw_size'] and raw<section['raw_offset']+section['raw_size'] and raw+size>section['raw_offset']:
                    raise ValueError('overlapping file sections')
            self.sections.append(dict(name=name,rva=rva,virtual_size=vsize,raw_offset=raw,raw_size=size,flags=flags))

    def section(self,name):
        found=[s for s in self.sections if s['name']==name]
        if len(found)!=1: raise ValueError(f'unique section {name} required')
        return found[0]

    def checksum(self):
        total=0
        for offset in range(0,len(self.data),2):
            if self.checksum_offset<=offset<self.checksum_offset+4: continue
            word=self.data[offset]+((self.data[offset+1] if offset+1<len(self.data) else 0)<<8)
            total+=word
            total=(total&0xffff)+(total>>16)
        total=(total&0xffff)+(total>>16)
        return (total+len(self.data))&0xffffffff
