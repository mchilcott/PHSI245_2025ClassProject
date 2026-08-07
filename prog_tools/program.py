import sys
import os

import chprog

if os.name == "nt":
    # There's an issue with loading incompatible libusb from the wrong place. This resolves that.
    print("Fixing PATH for windows libusb")
    os.environ["PATH"] = os.environ["WINDIR"] + r"\System32;" + os.environ["PATH"]

sys.exit(chprog._main())
