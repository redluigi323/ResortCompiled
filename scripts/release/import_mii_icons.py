#!/usr/bin/env python3
"""Convert Wheel Wizard's GPL-3.0 Mii picker drawings into Qt-readable SVGs.
Usage: python scripts/release/import_mii_icons.py /path/to/WheelWizard
The source revision and license are recorded in launcher/THIRD-PARTY.md.
"""
import html
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

source = Path(sys.argv[1]) / 'WheelWizard/Views/Styles/Resources/MiiIcons'
target = Path(__file__).resolve().parents[2] / 'launcher/assets/mii'
target.mkdir(parents=True, exist_ok=True)
ns = {'a': 'https://github.com/avaloniaui'}
colors = ['#efcfb0', '#39464a', '#42352d', '#367e98', '#1e5266']

def color(value):
    if not value:
        return 'none'
    match = re.fullmatch(r'\{StaticResource TemplateColor(\d+)\}', value)
    return colors[min(int(match[1])-1, len(colors)-1)] if match else value

for file in sorted(source.glob('*.axaml')):
    root = ET.parse(file).getroot()
    for drawing in root.findall('a:DrawingImage', ns):
        key = drawing.attrib['{http://schemas.microsoft.com/winfx/2006/xaml}Key']
        elements = []
        for shape in drawing.findall('.//a:GeometryDrawing', ns):
            geometry = shape.get('Geometry', '')
            rule = 'nonzero' if geometry.startswith('F1') else 'evenodd'
            geometry = re.sub(r'^F[01]\s*', '', geometry)
            pen = shape.find('a:GeometryDrawing.Pen/a:Pen', ns)
            stroke = '' if pen is None else (f' stroke="{color(pen.get("Brush"))}" '
                f'stroke-width="{pen.get("Thickness", "1")}" stroke-linejoin="round"')
            elements.append(f'<path d="{html.escape(geometry, quote=True)}" '
                            f'fill="{color(shape.get("Brush"))}" fill-rule="{rule}"{stroke}/>')
        (target / (key + '.svg')).write_text(
            '<!-- Adapted from Team Wheel Wizard; GPL-3.0-only. See THIRD-PARTY.md. -->\n'
            '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 52 52">' + ''.join(elements) + '</svg>\n')
