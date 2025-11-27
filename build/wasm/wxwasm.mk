WXCONFIG=$(EMSCRIPTEN)/system/local/bin/wx-config

WX_CXXFLAGS:=$(shell $(WXCONFIG) --cxxflags)
WX_LDFLAGS:=$(shell $(WXCONFIG) --libs base,core,html)
