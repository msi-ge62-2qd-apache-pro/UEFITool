PROJECT(UEFIExtract)

SET(PROJECT_SOURCES 
 uefiextract_main.cpp 
 uefiextract.cpp 
 ../bstrlib/bstrlib.c 
 ../bstrlib/bstrwrap.cpp 
 ../common/types.cpp 
 ../common/descriptor.cpp 
 ../common/ffs.cpp 
 ../common/ffsparser.cpp 
 ../common/fitparser.cpp 
 ../common/peimage.cpp 
 ../common/treeitem.cpp 
 ../common/treemodel.cpp 
 ../common/utility.cpp 
 ../common/LZMA/LzmaDecompress.c 
 ../common/LZMA/SDK/C/LzmaDec.c 
 ../common/Tiano/EfiTianoDecompress.c 
)

SET(PROJECT_HEADERS 
 ../bstrlib/bstrlib.h 
 ../common/bytearray.h 
 ../common/cbstring.h 
 ../common/basetypes.h 
 ../common/descriptor.h 
 ../common/gbe.h 
 ../common/me.h 
 ../common/ffs.h 
 ../common/ffsparser.h 
 ../common/fitparser.h
 ../common/peimage.h 
 ../common/types.h 
 ../common/treeitem.h 
 ../common/treemodel.h 
 ../common/utility.h 
 ../common/LZMA/LzmaDecompress.h 
 ../common/Tiano/EfiTianoDecompress.h 
)

ADD_EXECUTABLE(UEFIExtract ${PROJECT_SOURCES} ${PROJECT_HEADERS})