import argparse
import json
from pathlib import Path
import sys
from .patch import inspect,make_manifest,apply,rollback,write_new


def main():
    p=argparse.ArgumentParser(description='Analyze and patch the owned strategy host executable')
    sub=p.add_subparsers(dest='command',required=True)
    q=sub.add_parser('inspect'); q.add_argument('image')
    q=sub.add_parser('manifest'); q.add_argument('image'); q.add_argument('--features',type=int,default=15)
    q=sub.add_parser('apply'); q.add_argument('image'); q.add_argument('manifest'); q.add_argument('output')
    q=sub.add_parser('rollback'); q.add_argument('image'); q.add_argument('receipt'); q.add_argument('output')
    args=p.parse_args()
    try:
        data=Path(args.image).read_bytes()
        if args.command=='inspect': result=inspect(data)
        elif args.command=='manifest': result=make_manifest(data,args.features)
        elif args.command=='apply':
            candidate,result=apply(data,json.loads(Path(args.manifest).read_bytes()))
            write_new(args.output,candidate)
        else:
            original=rollback(data,json.loads(Path(args.receipt).read_bytes()))
            write_new(args.output,original)
            result={'restored':args.output}
        print(json.dumps(result,indent=2))
    except (OSError,ValueError,KeyError,TypeError) as exc:
        print(f'patch error: {exc}',file=sys.stderr)
        return 2
    return 0


if __name__=='__main__': sys.exit(main())
