import os
import lit.formats

config.name = "Axio"
config.test_format = lit.formats.ShTest(True)
config.suffixes = [".ax"]
config.test_source_root = os.path.dirname(__file__)
config.test_exec_root = config.test_source_root

config.substitutions.append(
    ("%axc", os.path.join(config.test_exec_root, "..", "build", "axc"))
)
config.substitutions.append(("%python", "python3"))
config.substitutions.append(("%filecheck", "FileCheck"))
