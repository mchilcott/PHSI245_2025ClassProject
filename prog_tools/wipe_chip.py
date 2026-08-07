import sys
from chprog import Programmer

def _main():
    try:
        print('Connecting to bootloader ...')
        isp = Programmer()
        isp.detect()
        print('Found %s with bootloader v%s.' % (isp.chipname, isp.bootloader))
        print('Erasing %s bytes' % (isp.code_flash_size))
        isp.erase(isp.code_flash_size)
        isp.exit()
    except Exception as ex:
        sys.stderr.write('ERROR: %s!\n' % str(ex))
        sys.exit(1)
    print('DONE.')
    sys.exit(0)
    

if __name__ == "__main__":
    _main()

