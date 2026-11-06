"""Turn the SILSO monthly sunspot file into the record of a terraflux.solar.cycle-run/v1 request.

SILSO (Royal Observatory of Belgium, https://www.sidc.be/SILSO/) publishes the monthly mean total sunspot number
(version 2) as SN_m_tot_V2.0.csv: one line per month, semicolon-separated,

    year;month;decimal year;sunspot number;standard deviation;observations;definitive (1) or provisional (0)

with -1 for a missing value. This tool reads that file (downloaded separately; nothing is fetched here), keeps
the months from --start onward, and writes a request whose record is `"kind": "monthly"`: the other sections are
copied from a template request (by default examples/solar-cycle-request.json). Missing months are refused unless
--fill-gaps interpolates them linearly (reported in data_origin). The first minimum after --start must be cycle
--first-cycle (SILSO numbering: cycle 1 starts in 1755, cycle 10 in 1855.9).

    python3 tools/silso_to_request.py SN_m_tot_V2.0.csv --start 1850-01 --first-cycle 10 --issue 2026.8 --output my-solar-request.json

Licence: SILSO data are CC BY-NC 4.0; cite "SILSO World Data Center, Royal Observatory of Belgium, Brussels".
"""
import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]


def read_silso(text):
    """[(year, month, value or None, definitive)] from the SILSO monthly CSV."""
    rows = []
    for number, line in enumerate(text.splitlines(), start=1):
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        parts = [p.strip() for p in line.split(';')]
        if len(parts) < 4:
            raise ValueError(f'line {number}: expected at least 4 semicolon-separated fields')
        try:
            year, month, value = int(parts[0]), int(parts[1]), float(parts[3])
        except ValueError as error:
            raise ValueError(f'line {number}: {error}') from None
        if not 1 <= month <= 12:
            raise ValueError(f'line {number}: month {month} out of range')
        definitive = len(parts) < 7 or parts[6] == '1'
        rows.append((year, month, None if value < 0 else value, definitive))
    for (y0, m0, _, _), (y1, m1, _, _) in zip(rows, rows[1:]):
        if y1 * 12 + m1 != y0 * 12 + m0 + 1:
            raise ValueError(f'months must be consecutive: {y0}-{m0:02d} is followed by {y1}-{m1:02d}')
    if not rows:
        raise ValueError('no monthly values found')
    return rows


def monthly_record(rows, start, first_cycle, fill_gaps=False, source='SN_m_tot_V2.0.csv'):
    """The request's record section from the rows at and after `start` ("YYYY-MM")."""
    year, month = (int(x) for x in start.split('-'))
    kept = [r for r in rows if r[0] * 12 + r[1] >= year * 12 + month]
    if not kept:
        raise ValueError(f'no months at or after {start}')
    if kept[0][0] != year or kept[0][1] != month:
        raise ValueError(f'the file does not contain {start}')
    values = [r[2] for r in kept]
    gaps = [i for i, v in enumerate(values) if v is None]
    if gaps and not fill_gaps:
        first = kept[gaps[0]]
        raise ValueError(f'{len(gaps)} missing months (first {first[0]}-{first[1]:02d}); use --fill-gaps to interpolate them')
    if gaps:
        known = [i for i, v in enumerate(values) if v is not None]
        if not known:
            raise ValueError('every month is missing')
        for i in gaps:
            before = max((k for k in known if k < i), default=None)
            after = min((k for k in known if k > i), default=None)
            if before is None or after is None:
                values[i] = values[after if before is None else before]
            else:
                w = (i - before) / (after - before)
                values[i] = round(values[before] + w * (values[after] - values[before]), 1)
    provisional = sum(1 for r in kept if not r[3])
    last = kept[-1]
    origin = (f'SILSO World Data Center, Royal Observatory of Belgium: monthly mean total sunspot number v2 ({source}), '
              f'{kept[0][0]}-{kept[0][1]:02d} to {last[0]}-{last[1]:02d}; {provisional} provisional months; '
              f'{len(gaps)} missing months interpolated. CC BY-NC 4.0.')
    return {'kind': 'monthly', 'data_origin': origin, 'first_cycle_number': first_cycle, 'first_year': kept[0][0], 'first_month': kept[0][1], 'values': values}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('silso_csv', type=Path)
    parser.add_argument('--template', type=Path, default=ROOT / 'examples/solar-cycle-request.json')
    parser.add_argument('--start', default='1850-01', help='first month kept, YYYY-MM (default 1850-01)')
    parser.add_argument('--first-cycle', type=int, default=10, help='number of the cycle starting at the first minimum after --start (default 10)')
    parser.add_argument('--issue', type=float, help='forecast issue year (default: the template\'s, capped at the end of the file)')
    parser.add_argument('--fill-gaps', action='store_true', help='interpolate missing months linearly')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        rows = read_silso(args.silso_csv.read_text())
        request = json.loads(args.template.read_text())
        request['record'] = monthly_record(rows, args.start, args.first_cycle, args.fill_gaps, args.silso_csv.name)
        end = rows[-1][0] + rows[-1][1] / 12.0   # the end of the last month
        issue = args.issue if args.issue is not None else min(request['forecast']['issue_year'], end)
        if issue > end + 1e-9:
            raise ValueError(f'the issue year {issue} lies after the end of the file ({end:.3f})')
        request['forecast']['issue_year'] = issue
        request['forecast']['horizon_year'] = max(request['forecast']['horizon_year'], issue + 2.0)
    except (OSError, ValueError, KeyError) as error:
        print(f'error: {error}', file=sys.stderr)
        return 1
    args.output.write_text(json.dumps(request, indent=2, ensure_ascii=False) + '\n')
    print(f'Wrote {args.output} ({len(request["record"]["values"])} months, issue {issue})')
    return 0


if __name__ == '__main__':
    sys.exit(main())
