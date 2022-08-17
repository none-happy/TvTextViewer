EXE = text_viewer
IMGUI_DIR = 3rd_party/imgui
CXXOPTS_DIR = 3rd_party/cxxopts

SOURCES = main.cpp imgui_impl_sdl.cpp view.cpp
SOURCES += $(IMGUI_DIR)/imgui.cpp $(IMGUI_DIR)/imgui_draw.cpp $(IMGUI_DIR)/imgui_tables.cpp $(IMGUI_DIR)/imgui_widgets.cpp
SOURCES += $(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp
OBJS = $(addsuffix .o, $(basename $(notdir $(SOURCES))))

CXXFLAGS = -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends -I$(CXXOPTS_DIR)/include
CXXFLAGS += -std=c++17 -O2 -Wall -Wformat
CXXFLAGS += -DIMGUI_IMPL_OPENGL_ES2
CXXFLAGS += `sdl2-config --cflags`
LIBS = -lGLESv2 -ldl `sdl2-config --libs`

##---------------------------------------------------------------------
## BUILD RULES
##---------------------------------------------------------------------

%.o:%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.o:$(IMGUI_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.o:$(IMGUI_DIR)/backends/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

all: $(EXE)
	@echo Build complete

$(EXE): $(OBJS)
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LIBS)

clean:
	rm -f $(EXE) $(OBJS)
