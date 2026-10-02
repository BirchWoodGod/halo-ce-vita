#!/usr/bin/env python3
"""Tag struct layouts for Xbox Halo CE cache files, from Invader's definitions
(invader/src/tag/hek/definition/*.json): for every struct, the offsets of the
fields that hold pointers into the tag cache window, and nested structs."""
import glob, json, os, re

# a checkout of https://github.com/SnowyMouse/invader: INVADER=<path>, else ./invader
INVADER_DIR = os.environ.get('INVADER') or os.path.join(os.path.dirname(__file__), '..', 'invader')
DEFINITION_DIR = os.path.join(INVADER_DIR, 'src', 'tag', 'hek', 'definition')
FOURCC_HPP = os.path.join(INVADER_DIR, 'include', 'invader', 'hek', 'fourcc.hpp')

PRIMITIVE_SIZES = {
    'float': 4, 'int8': 1, 'uint8': 1, 'int16': 2, 'uint16': 2, 'int32': 4, 'uint32': 4,
    'Angle': 4, 'Fraction': 4, 'Index': 2, 'TagID': 4, 'Pointer': 4, 'TagFourCC': 4, 'TagString': 32,
    'ColorARGBInt': 4, 'ColorRGB': 12, 'ColorARGB': 16, 'Point2D': 8, 'Point3D': 12, 'Vector2D': 8,
    'Vector3D': 12, 'Point2DInt': 4, 'Euler2D': 8, 'Euler3D': 12, 'Plane2D': 12, 'Plane3D': 16,
    'Quaternion': 16, 'Matrix': 36, 'Rectangle2D': 8, 'ScenarioScriptNodeValue': 4,
    'TagDependency': 16, 'TagReflexive': 12, 'TagDataOffset': 20,
}

# uint32 fields that hold pointers in Xbox cache files (Invader documents them as such)
XBOX_POINTER_FIELDS = {
    ('ModelGeometryPart', 'triangle offset'), ('ModelGeometryPart', 'triangle offset 2'),
    ('ModelGeometryPart', 'vertex offset'),
    ('ScenarioBSP', 'bsp address'),
}


def load():
    objects = {}
    for path in sorted(glob.glob(os.path.join(DEFINITION_DIR, '*.json'))):
        for o in json.load(open(path)):
            objects[o['name']] = o
    return objects


class Layout:
    def __init__(self):
        self.objects = load()
        self.sizes = {}
        self.fields = {}   # struct -> list of (offset, kind, child)
        # 'Soul': a block of widget definition references; 'boom': a radius and padding
        self.objects['UIWidgetCollectionReference'] = {'name': 'UIWidgetCollectionReference', 'type': 'struct', 'size': 16,
            'fields': [{'name': 'ui widget definition', 'type': 'TagDependency'}]}
        self.objects['UIWidgetCollection'] = {'name': 'UIWidgetCollection', 'type': 'struct', 'size': 12,
            'fields': [{'name': 'ui widget definitions', 'type': 'TagReflexive', 'struct': 'UIWidgetCollectionReference'}]}
        self.objects['Spheroid'] = {'name': 'Spheroid', 'type': 'struct', 'size': 4,
            'fields': [{'name': 'radius', 'type': 'float'}]}
        for name, o in self.objects.items():
            if o['type'] == 'struct':
                self.layout(name)

    def type_size(self, t):
        if t in PRIMITIVE_SIZES:
            return PRIMITIVE_SIZES[t]
        o = self.objects[t]
        if o['type'] == 'enum':
            return 2
        if o['type'] == 'bitfield':
            return o['width'] // 8
        return self.layout(t)

    def layout(self, name):
        if name in self.sizes:
            return self.sizes[name]
        o = self.objects[name]
        fields = []
        offset = 0
        if o.get('inherits'):
            offset = self.layout(o['inherits'])
            fields.extend(self.fields[o['inherits']])
        for f in o['fields']:
            if isinstance(f, str):
                continue
            t = f['type']
            if t == 'pad':
                offset += f['size']
                continue
            if t == 'editor_section':
                continue
            count = f.get('count', 1) * (2 if f.get('bounds') else 1)
            size = self.type_size(t)
            for k in range(count):
                at = offset + k * size
                if t == 'TagReflexive':
                    fields.append((at, 'block', f['struct']))
                elif t == 'TagDataOffset':
                    fields.append((at, 'data', None))
                elif t == 'TagDependency':
                    fields.append((at, 'reference', None))
                elif t == 'Pointer' or (name, f.get('name')) in XBOX_POINTER_FIELDS:
                    fields.append((at, 'pointer', None))
                elif t in self.objects and self.objects[t]['type'] == 'struct':
                    fields.append((at, 'struct', t))
            offset += size * count
        declared = o.get('size')
        if declared is not None and declared != offset:
            raise SystemExit(f'{name}: computed size {offset}, declared {declared}')
        self.sizes[name] = offset
        self.fields[name] = fields
        return offset

    def groups(self):
        """fourcc -> root struct"""
        text = open(FOURCC_HPP).read()
        fourccs = {m.group(1).lower(): int(m.group(2), 16) for m in re.finditer(r'TAG_FOURCC_(\w+) = (0x[0-9A-Fa-f]+)', text)}
        result = {}
        for name, o in self.objects.items():
            if o['type'] == 'struct' and o.get('class') and o['class'] in fourccs:
                result[fourccs[o['class']]] = name
        # groups Invader has no definition for (their layouts are trivial)
        result[0x536F756C] = 'UIWidgetCollection'   # 'Soul'
        result[0x626F6F6D] = 'Spheroid'             # 'boom'
        return result


if __name__ == '__main__':
    layout = Layout()
    groups = layout.groups()
    print(len(layout.sizes), 'structs laid out,', len(groups), 'groups')
    for fourcc, name in sorted(groups.items()):
        print(f'{fourcc.to_bytes(4, "big").decode()} {name} {layout.sizes[name]}')
