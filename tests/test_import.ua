; test_import.ua — tests @IMPORT and duplicate prevention
LDI R0, 10

@IMPORT "test_import_lib.ua"

; second import of same file — should be skipped
@IMPORT "test_import_lib.ua"

HLT
