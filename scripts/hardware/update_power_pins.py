#!/usr/bin/env python3
"""Copy Pins_v3.ods to v4, preserving ZIP members/styles; map saved KiCad nets.
Usage: update_power_pins.py NETLIST_XML SOURCE_ODS DESTINATION_ODS
Generate the netlist with kicad-cli sch export netlist --format kicadxml.
"""
import sys
from copy import deepcopy
from pathlib import Path
from zipfile import ZipFile
from lxml import etree as E
netlist, source, destination = map(Path, sys.argv[1:])
if destination.exists():
    raise SystemExit(f'Refusing to overwrite {destination}')
nets = E.parse(str(netlist)).getroot().find('nets')
components = {c.get('ref'): c for c in E.parse(str(netlist)).getroot().find('components')}

def connection(ref, pin):
    for net in nets:
        if any(n.get('ref') == ref and n.get('pin') == str(pin) for n in net.findall('node')):
            return net.get('name'), ', '.join(n.get('ref') + '.' + n.get('pin') + ' ' + n.get('pinfunction', '') for n in net.findall('node') if n.get('ref') != ref)
    return 'unconnected', ''

with ZipFile(source) as archive:
    root = E.fromstring(archive.read('content.xml'))
    ns = root.nsmap
    def tag(prefix, name): return '{' + ns[prefix] + '}' + name
    sheets = root.findall('.//' + tag('table', 'table'))
    teensy = next(t for t in sheets if t.get(tag('table', 'name')) == 'T4_1')
    def text(cell): return ''.join(cell.itertext()).strip()
    def write(cell, value):
        # Preserve visual cell styles, replace only value/formula/content.
        for key in list(cell.attrib):
            if key.startswith('{' + ns['office'] + '}') or key == tag('table', 'formula'):
                del cell.attrib[key]
        for child in list(cell): cell.remove(child)
        cell.set(tag('office', 'value-type'), 'string')
        E.SubElement(cell, tag('text', 'p')).text = str(value)
    functions = {
        0:'WS2812B RGB LED data (level shifted)',1:'Amplifier enable / shutdown',2:'74HC165 / NES clock',
        3:'NES / 74HC165 parallel-load latch',4:'NES serial data (JP12 selection)',5:'MCP23008 active-low interrupt (U5.8)',
        6:'Unused / unconnected',7:'I2S audio data out',8:'I2S audio data in',9:'Motor enable net (no load connected)',
        10:'Slot 2 SPI0 chip select',11:'SPI0 MOSI / main display',12:'SPI0 MISO / main display',13:'SPI0 SCK / main display',
        14:'Slot 2 TX3 / main display reset',15:'Slot 2 RX3 / main display backlight',
        16:'I2C1 SCL: STUSB4500, INA237, MCP23008, environment, slot 1',
        17:'I2C1 SDA: STUSB4500, INA237, MCP23008, environment, slot 1',
        18:'Audio I2C0 SDA',19:'Audio I2C0 SCL',20:'I2S LRCLK',21:'I2S BCLK',22:'Unused / unconnected (old relay removed)',
        23:'I2S MCLK',24:'I2C2 SCL / slots 0 and 2',25:'I2C2 SDA / slots 0 and 2',26:'SPI1 shared MOSI',
        27:'SPI1 shared SCK',28:'Slot 0 RX7',29:'Slot 0 TX7',30:'Small display reset',31:'Small display command / data',
        32:'Small display chip select',33:'Small display PWM backlight',34:'Slot 1 RX8',35:'Slot 1 TX8',
        36:'Slot 1 SPI1 chip select',37:'Slot 0 SPI1 chip select',38:'Spare GPIO38 (old VIN monitor removed)',
        39:'SPI1 shared MISO',40:'DMM analog input A16',41:'Volume potentiometer A17'}
    mapping = {}
    for net in nets:
        for node in net.findall('node'):
            if node.get('ref') == 'U1':
                first = node.get('pinfunction', '').split('_')[0]
                if first.isdigit() and int(first) <= 41:
                    mapping[int(first)] = net.get('name')
    for row in teensy.findall(tag('table','table-row')):
        cells = row.findall(tag('table','table-cell'))
        if not cells or not text(cells[0]).isdigit(): continue
        pin = int(text(cells[0]))
        if pin not in functions: continue
        while len(cells) < 3:
            cells.append(E.SubElement(row,tag('table','table-cell')))
        write(cells[1], functions[pin]); write(cells[2],mapping.get(pin,'unconnected'))
    # Wrap new pin descriptions and give the added sheets readable column widths.
    automatic=root.find(tag('office','automatic-styles'))
    for name,header in [('PowerText',False),('PowerHeader',True)]:
        style=E.SubElement(automatic,tag('style','style'),{tag('style','name'):name,tag('style','family'):'table-cell'})
        props={tag('fo','wrap-option'):'wrap',tag('style','vertical-align'):'top'}
        if header: props[tag('fo','background-color')]='#d9e8f5'
        E.SubElement(style,tag('style','table-cell-properties'),props)
        if header:E.SubElement(style,tag('style','text-properties'),{tag('fo','font-weight'):'bold'})
    for index,width in enumerate([2.2,2.1,3.8,7.8,7.2,8.5,9.0]):
        style=E.SubElement(automatic,tag('style','style'),{tag('style','name'):f'PowerCol{index}',tag('style','family'):'table-column'})
        E.SubElement(style,tag('style','table-column-properties'),{tag('style','column-width'):f'{width}cm'})
    # New sheets preserve existing legacy sheets and their layout.
    template = teensy.findall(tag('table','table-row'))[1].findall(tag('table','table-cell'))[1]
    def sheet(name, rows):
        table = E.Element(tag('table','table'), {tag('table','name'):name})
        for index in range(max(map(len,rows))):
            E.SubElement(table,tag('table','table-column'),{tag('table','style-name'):f'PowerCol{index}'})
        for row_index,values in enumerate(rows):
            row=E.SubElement(table,tag('table','table-row'))
            for value in values:
                cell=deepcopy(template);cell.attrib.pop(tag('table','number-columns-repeated'),None)
                write(cell,value);cell.set(tag('table','style-name'),'PowerHeader' if row_index==0 else 'PowerText');row.append(cell)
        teensy.getparent().append(table)
    gpio = [
        ('GP0',10,'Input','LTC4417 VALID1_N / USB-PD input valid','Low = valid; highest priority'),
        ('GP1',11,'Input','LTC4417 VALID2_N / DC input valid','Low = valid; second priority'),
        ('GP2',12,'Input','LTC4417 VALID3_N / auxiliary input valid','Low = valid; third priority'),
        ('GP3',13,'Output','TPS25820 EN / USB host power','High = enable; 100k pulldown; OFF at boot'),
        ('GP4',14,'Input','TPS25820 FAULT_N','Low = fault; disable and latch until explicit clear'),
        ('GP5',15,'Input','TPS25820 SINK_N','Low = USB host sink attached (not proof of VBUS)'),
        ('GP6',16,'Input / open-drain emulation','INA237 ALERT_N + LTC2954 KILL_N','Shared line: never drive HIGH; input normally, output LOW only for final shutdown'),
        ('GP7',17,'Input','LTC2954 INT_N / power button','Low = request graceful shutdown')]
    rows=[['MCP23008 U5 GPIO','Package pin','Firmware direction','Function','Schematic net','Connected pins','Meaning']]
    for gp,pin,direction,function,note in gpio:
        net,peers=connection('U5',pin);rows.append([gp,pin,direction,function,net,peers,note])
    rows += [['INT',8,'Output -> Teensy GPIO5','Active-low open-drain interrupt','GPIO Exp INT','U1 package pin 7 / GPIO5','Firmware polls GPIO as fallback'],
             ['SCL / SDA','1 / 2','I2C1','Teensy16 SCL / Teensy17 SDA','SCL1 / SDA1','','Default address 0x20 if JP15/16/17 bridge pads1-2 (GND)']]
    sheet('MCP23008_Power',rows)
    rows=[['Device','Reference','Bus / address','Pins / function','Notes'],
          ['LTC4417','U4','GPIO via U5','VALID1/2/3_N -> GP0/1/2; SHDN_N <- U7 EN','V1 USB-PD, V2 DC, V3 auxiliary; selection inferred from priority, not measured gate state'],
          ['LTC2954-1','U7','GPIO via U5','INT_N -> GP7; KILL_N <-> GP6/INA237 ALERT_N; EN -> U4 SHDN_N','Graceful button shutdown; hardware current trip cuts power directly'],
          ['MCP23008','U5','I2C1 / default0x20','GPIO0..7 listed on MCP23008_Power sheet','JP15 A0 / JP16 A1 / JP17 A2'],
          ['INA237','U13','I2C1 / default0x40','ALERT_N -> GP6 + U7 KILL_N; shunt R13 = 10 mΩ','JP13 A1 / JP14 A0; nominal shunt, not calibrated'],
          ['TPS25820DSSR','U22','GPIO via U5','EN GP3 / FAULT_N GP4 / SINK_N GP5','CHG grounded: default USB current advertisement; 5V source, not PD negotiator'],
          ['STUSB4500QTR','U3','I2C1 / default0x28','SCL16 / SDA17; ALERT unconnected','JP4 A0 / JP5 A1; passive status at boot, pd command for explicit requests'],
          ['Schematic source','2026-10-19','Motherboard/SuperKeyboardCPU.kicad_sch','Power.kicad_sch','Saved KiCad XML netlist; default addresses assume bridged1-2 footprints'],
          ['Review needed','U4','Hardware','UV1/2/3 and OV1/2/3 are unconnected','Firmware cannot establish input validity windows without hardware dividers'],
          ['Review needed','R13','Hardware','10 mΩ shunt assigned Sensirion humidity-sensor footprint','Confirm actual shunt symbol, Kelvin pad mapping and footprint before fabrication'],
          ['Supply confirmed','INA237','Upstream 3.3V','Dedicated regulator before LTC4417 MOSFETs; user confirmed 2026-10-19','Configured trip survives switched-system shutdown while upstream supply remains; full supply loss clears volatile INA settings'],
          ['Suggested shunt','R13','WSK2512R0100FEA / 10mOhm / 1W / four terminals','Resistor_SMD:R_Shunt_Vishay_WSK2512_6332Metric_T1.19mm','Force pads1/4, sense2/3. Move IN+ to upstream sense3; IN- to downstream sense2; VBUS to load-side bus. Check orientation/pin mapping.'],
          ['DigiKey Canada','R13','541-WSK2512R0100FEACT-ND','https://www.digikey.ca/en/products/detail/vishay-dale/WSK2512R0100FEA/12142494','Listed out of stock/backorder 2026-10-19; check when ordering'],
          ['Review needed','Power rails','Hardware','+3.3V and +3V3 are distinct schematic nets','Verify retained INA supply and I2C/ALERT pullups with switched MCU/expander rail off']]
    sheet('Power_Hardware_v4',rows)
    content=E.tostring(root,xml_declaration=True,encoding='UTF-8')
    with ZipFile(destination,'w') as out:
        for member in archive.infolist():
            out.writestr(member, content if member.filename=='content.xml' else archive.read(member.filename))
print(destination)
