#!/usr/bin/env python3
"""Compare AI trainer results produced by the port against retail AIConfig.xml.

Retail's AIConfig.xml (EURO/BNK/DataBase, TEA-encrypted) stores, per car, the
coefficients the retail AI trainer (GMAITrainer, game mode 19) fitted:

    braking distance (m)    d(v) = braking_dist_c1 * v + braking_dist_c2 * v^2
    max corner speed (m/s)  s(r) = turning_speed_c1 * sqrt(r) + turning_speed_c2 * r

with v in m/s and r in metres. The port's trainer writes the same models to
DataBase/AiTrainer.xml (BrakeCoef1/2, TurnCoef1/2) together with the raw
BrakeTest / TurnSpeedTest samples. Records without samples are just copies of
the AIConfig values (GMAITrainer::loadTrainerResults) and are skipped.

Usage:
    aitrainer_compare.py AiTrainer.xml AIConfig.xml [--csv out.csv]
                         [--speeds 50,100,150,200] [--radii 20,50,100,200]

AIConfig.xml may be the encrypted retail file or an already decrypted copy.
"""

import argparse
import csv
import math
import re
import struct
import sys

TEA_KEYS_BANK = (0x4FE23C4A, 0x80BAC211, 0x6917BD3A, 0xF0528EBD)
MASK32 = 0xFFFFFFFF


def tea_decrypt_block(v0, v1):
    total = 0xC6EF3720
    for _ in range(32):
        v1 = (v1 - ((((v0 >> 5) ^ (v0 << 4)) + v0) ^
                    (TEA_KEYS_BANK[(total >> 11) & 3] + total))) & MASK32
        total = (total + 0x61C88647) & MASK32
        v0 = (v0 - ((((v1 >> 5) ^ (v1 << 4)) + v1) ^
                    (TEA_KEYS_BANK[total & 3] + total))) & MASK32
    return v0, v1


def decrypt_bank_tea(data):
    # Bank::DecryptTEA (FUN_006dde40): block i ^= TEA^-1(ciphertext block i+1)
    buffer = bytearray(data)
    for i in range((len(buffer) - 8) // 8):
        next0, next1 = struct.unpack_from('<II', buffer, i * 8 + 8)
        key0, key1 = tea_decrypt_block(next0, next1)
        cur0, cur1 = struct.unpack_from('<II', buffer, i * 8)
        struct.pack_into('<II', buffer, i * 8, cur0 ^ key0, cur1 ^ key1)
    return bytes(buffer)


def read_xml_text(path):
    data = open(path, 'rb').read()
    if not data.lstrip().startswith(b'<'):
        data = decrypt_bank_tea(data)
    return data.decode('latin-1', errors='replace')


ATTRIB_RE = re.compile(r'([A-Za-z_][A-Za-z0-9_]*)\s*=\s*"([^"]*)"')


def parse_attribs(tag_body):
    return {name: value for name, value in ATTRIB_RE.findall(tag_body)}


def parse_retail_physics(text):
    section = re.search(r'<VEHICLE_PHYSICS>(.*?)</VEHICLE_PHYSICS>', text,
                        re.S)
    if section is None:
        sys.exit('AIConfig.xml: no VEHICLE_PHYSICS section')

    physics = {}
    for tag_body in re.findall(r'<VEHICLE\b([^>]*)/?>', section.group(1)):
        attribs = parse_attribs(tag_body)
        if 'database_id' not in attribs:
            continue
        physics[int(attribs['database_id'])] = {
            'brake_c1': float(attribs.get('braking_dist_c1', 0.3)),
            'brake_c2': float(attribs.get('braking_dist_c2', 0.033)),
            'turn_c1': float(attribs.get('turning_speed_c1', 2.8)),
            'turn_c2': float(attribs.get('turning_speed_c2', 0.0)),
        }
    return physics


def parse_port_results(text):
    results = {}
    pattern = re.compile(r'<Result\b([^>]*?)(/>|>(.*?)</Result>)', re.S)
    for match in pattern.finditer(text):
        attribs = parse_attribs(match.group(1))
        children = match.group(3) or ''
        brake = [(float(a['Speed']), float(a['Dist']))
                 for a in map(parse_attribs,
                              re.findall(r'<BrakeTest\b([^>]*)/?>', children))]
        turn = [(float(a['Radius']), float(a['MaxSpeed']))
                for a in map(parse_attribs,
                             re.findall(r'<TurnSpeedTest\b([^>]*)/?>',
                                        children))]
        results[int(attribs['CarId'])] = {
            'brake_c1': float(attribs.get('BrakeCoef1', 0.0)),
            'brake_c2': float(attribs.get('BrakeCoef2', 0.0)),
            'brake_err': float(attribs.get('BrakeStdErr', 0.0)),
            'turn_c1': float(attribs.get('TurnCoef1', 0.0)),
            'turn_c2': float(attribs.get('TurnCoef2', 0.0)),
            'turn_err': float(attribs.get('TurnStdErr', 0.0)),
            'brake_samples': brake,
            'turn_samples': turn,
        }
    return results


def brake_model(c1, c2, speed):
    return c1 * speed + c2 * speed * speed


def turn_model(c1, c2, radius):
    return c1 * math.sqrt(radius) + c2 * radius


def relative_error(port, retail):
    return (port - retail) / retail if retail != 0.0 else float('nan')


def rms(values):
    return math.sqrt(sum(v * v for v in values) / len(values)) if values else 0.0


def median(values):
    ordered = sorted(values)
    n = len(ordered)
    if n == 0:
        return float('nan')
    return ordered[n // 2] if n % 2 else 0.5 * (ordered[n // 2 - 1] +
                                                ordered[n // 2])


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('port_xml', help='port DataBase/AiTrainer.xml')
    parser.add_argument('retail_xml', help='retail DataBase/AIConfig.xml')
    parser.add_argument('--csv', help='write per-car rows to this CSV file')
    parser.add_argument('--speeds', default='50,100,150,200',
                        help='brake reference speeds in km/h')
    parser.add_argument('--radii', default='20,50,100,200',
                        help='turn reference radii in metres')
    args = parser.parse_args()

    speeds_kmh = [float(s) for s in args.speeds.split(',')]
    radii = [float(r) for r in args.radii.split(',')]

    retail = parse_retail_physics(read_xml_text(args.retail_xml))
    port = parse_port_results(read_xml_text(args.port_xml))

    rows = []
    brake_errors = {s: [] for s in speeds_kmh}
    turn_errors = {r: [] for r in radii}
    brake_residuals = []
    turn_residuals = []

    for car_id in sorted(port):
        result = port[car_id]
        reference = retail.get(car_id)
        if reference is None:
            continue
        if not result['brake_samples'] and not result['turn_samples']:
            continue

        row = {'car_id': car_id,
               'brake_samples': len(result['brake_samples']),
               'turn_samples': len(result['turn_samples'])}

        if result['brake_samples']:
            for speed_kmh in speeds_kmh:
                speed = speed_kmh / 3.6
                error = relative_error(
                    brake_model(result['brake_c1'], result['brake_c2'], speed),
                    brake_model(reference['brake_c1'], reference['brake_c2'],
                                speed))
                brake_errors[speed_kmh].append(error)
                row['brake_%dkmh_err' % speed_kmh] = error
            residuals = [distance - brake_model(reference['brake_c1'],
                                                reference['brake_c2'], speed)
                         for speed, distance in result['brake_samples']]
            row['brake_rms_vs_retail_m'] = rms(residuals)
            row['brake_fit_stderr_m'] = result['brake_err']
            brake_residuals.append(row['brake_rms_vs_retail_m'])

        if result['turn_samples']:
            for radius in radii:
                error = relative_error(
                    turn_model(result['turn_c1'], result['turn_c2'], radius),
                    turn_model(reference['turn_c1'], reference['turn_c2'],
                               radius))
                turn_errors[radius].append(error)
                row['turn_%dm_err' % radius] = error
            residuals = [speed - turn_model(reference['turn_c1'],
                                            reference['turn_c2'], radius)
                         for radius, speed in result['turn_samples']]
            row['turn_rms_vs_retail_ms'] = rms(residuals)
            row['turn_fit_stderr_ms'] = result['turn_err']
            turn_residuals.append(row['turn_rms_vs_retail_ms'])

        rows.append(row)

    if not rows:
        sys.exit('No tested car in the port results matches a retail entry '
                 '(run a brake/turn campaign first).')

    print('%d tested cars matched against %d retail entries\n' %
          (len(rows), len(retail)))
    for row in rows:
        parts = ['car %5d' % row['car_id']]
        for speed_kmh in speeds_kmh:
            key = 'brake_%dkmh_err' % speed_kmh
            if key in row:
                parts.append('brake@%d %+6.1f%%' % (speed_kmh, row[key] * 100))
        for radius in radii:
            key = 'turn_%dm_err' % radius
            if key in row:
                parts.append('turn@%dm %+6.1f%%' % (radius, row[key] * 100))
        print('  '.join(parts))

    print('\nSummary (port vs retail model, signed error: median / mean |err|)')
    for speed_kmh, errors in brake_errors.items():
        if errors:
            print('  braking distance @ %3d km/h: %+6.1f%% / %5.1f%%  (n=%d)' %
                  (speed_kmh, median(errors) * 100,
                   sum(abs(e) for e in errors) / len(errors) * 100,
                   len(errors)))
    for radius, errors in turn_errors.items():
        if errors:
            print('  corner speed @ %3d m       : %+6.1f%% / %5.1f%%  (n=%d)' %
                  (radius, median(errors) * 100,
                   sum(abs(e) for e in errors) / len(errors) * 100,
                   len(errors)))
    if brake_residuals:
        print('  raw brake samples vs retail model, median RMS: %.2f m' %
              median(brake_residuals))
    if turn_residuals:
        print('  raw turn samples vs retail model, median RMS: %.2f m/s' %
              median(turn_residuals))

    if args.csv:
        keys = sorted({key for row in rows for key in row},
                      key=lambda k: (k != 'car_id', k))
        with open(args.csv, 'w', newline='') as stream:
            writer = csv.DictWriter(stream, fieldnames=keys)
            writer.writeheader()
            writer.writerows(rows)
        print('\nWrote %s' % args.csv)


if __name__ == '__main__':
    main()
