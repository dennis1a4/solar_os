"""Small deterministic local documents for host and device acceptance."""
import io,struct,zlib,zipfile

def fixtures():
    def chunk(kind,data):return struct.pack('!I',len(data))+kind+data+struct.pack('!I',zlib.crc32(kind+data)&0xffffffff)
    png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('!IIBBBBB',32,16,8,2,0,0,0))+chunk(b'IDAT',zlib.compress((b'\0'+bytes([40,120,200])*32)*16))+chunk(b'IEND',b'')
    container='''<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>'''
    opf='''<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Teensy Reader Test</dc:title><dc:identifier id="id">local-test</dc:identifier><dc:language>en</dc:language></metadata><manifest><item id="one" href="one.xhtml" media-type="application/xhtml+xml"/><item id="two" href="two.xhtml" media-type="application/xhtml+xml"/><item id="image" href="images/picture.png" media-type="image/png"/></manifest><spine><itemref idref="one"/><itemref idref="two"/></spine></package>'''
    first='<html xmlns="http://www.w3.org/1999/xhtml"><head><title>One</title></head><body><h1>First chapter</h1><p>Needle in a readable EPUB.</p><img src="images/picture.png" alt="color swatch"/>'+''.join('<p>Paragraph %d with bold <b>words</b> and ordinary text.</p>'%i for i in range(45))+'</body></html>'
    second='<html xmlns="http://www.w3.org/1999/xhtml"><body><h1>Second chapter</h1><p>Final page of the test book.</p></body></html>'
    book=io.BytesIO()
    with zipfile.ZipFile(book,'w',compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr('mimetype','application/epub+zip',compress_type=zipfile.ZIP_STORED)
        for name,data in [('META-INF/container.xml',container),('OPS/book.opf',opf),('OPS/one.xhtml',first),('OPS/two.xhtml',second),('OPS/images/picture.png',png)]:z.writestr(name,data)
    markdown='# SolarOS on Teensy\n\nA **bold** and *italic* paragraph. Needle for search.\n\n![Color](picture.png)\n\n'+''.join('## Section %d\n\nReading text for page %d.\n\n'%(i,i) for i in range(30))
    return {'book.epub':book.getvalue(),'reading.md':markdown.encode(),'picture.png':png,'draft.md':b'Original\n','broken.epub':b'not a ZIP archive'}

if __name__=='__main__':
    import sys
    from pathlib import Path
    root=Path(sys.argv[1]);root.mkdir(parents=True,exist_ok=True)
    for name,data in fixtures().items():(root/name).write_bytes(data)
