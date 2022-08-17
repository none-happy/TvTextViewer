/** Copyright (c) 2021 Nikolai Wuttke
  *
  * Permission is hereby granted, free of charge, to any person obtaining a copy
  * of this software and associated documentation files (the "Software"), to deal
  * in the Software without restriction, including without limitation the rights
  * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  * copies of the Software, and to permit persons to whom the Software is
  * furnished to do so, subject to the following conditions:
  *
  * The above copyright notice and this permission notice shall be included in all
  * copies or substantial portions of the Software.
  *
  * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  * SOFTWARE.
  */

#include "view.hpp"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl.h"
#include "imgui_impl_opengl3.h"

#include <cxxopts.hpp>
#include <GLES2/gl2.h>
#include <SDL.h>

#include <cstdlib>
#include <iostream>
#include <fstream>
#include <optional>


namespace
{

// Parses command line options and returns a ParseResult if successful.
// Returns an empty optional otherwise.
// This function defines all available command line arguments.
std::optional<cxxopts::ParseResult> parseArgs(int argc, char** argv)
{
    cxxopts::Options options(argv[0], "TvTextViewer - a full-screen text viewer");

    // Define command line options, add new options here.
    // This is using the cxxopts library. Refer to its documentation for more info:
    // https://github.com/jarro2783/cxxopts/wiki/Options
    options
      .positional_help("[input file]")
      .show_positional_help()
      .add_options()
        ("input_file", "text file to view", cxxopts::value<std::string>())
        ("s,script_file", "script outpout to view", cxxopts::value<std::string>())
        ("m,message", "text to show instead of viewing a file", cxxopts::value<std::string>())
        ("f,font_size", "font size in pixels", cxxopts::value<int>())
        ("t,title", "window title (filename by default)", cxxopts::value<std::string>())
        ("y,yes_button", "shows a yes button with different exit code")
        ("e,error_display", "format as error, background will be red")
        ("w,wrap_lines", "wrap long lines of text. WARNING: could be slow for large files!")
        ("h,help", "show help")
      ;

    // Allow the input file to be given as positional argument
    options.parse_positional({"input_file"});

    // Now parse the options and make sure they are valid
      const auto result = options.parse(argc, argv);

      // If -h/--help is given, just print the help text (auto-generated by
      // cxxopts) and exit.
      if (result.count("help"))
      {
        std::cout << options.help({""}) << '\n';
        std::exit(0);
      }

      // Verification: Make sure there's some input, otherwise print an error and
      // exit.
      if (!result.count("input_file") && !result.count("message") && !result.count("script_file"))
      {
        std::cerr << "Error: No input given\n\n";
        std::cerr << options.help({""}) << '\n';
        return {};
      }

      // Make sure that mutually exclusive options aren't used at the same time,
      // print an error and exit if so.
      if (result.count("input_file") && result.count("message"))
      {
        std::cerr << "Error: Cannot use input_file and message at the same time\n\n";
        std::cerr << options.help({""}) << '\n';
        return {};
      }

      // All verification steps passed, we can return the parsed options
      return result;

  return {};
}


// Converts escape sequences like `\n` into their character values.
// This mimicks the behavior of the `echo -e` UNIX command, albeit
// not all possible escape sequences are implemented.
//
// Compare https://github.com/wertarbyte/coreutils/blob/f70c7b785b93dd436788d34827b209453157a6f2/src/echo.c#L203
std::string replaceEscapeSequences(const std::string& original)
{
  std::string result;
  result.reserve(original.size());

  for (auto iChar = original.begin(); iChar != original.end(); ++iChar)
  {
    if (*iChar == '\\' && std::next(iChar) != original.end())
    {
      switch (*std::next(iChar))
      {
        case 'f': result.push_back('\f'); ++iChar; break;
        case 'n': result.push_back('\n'); ++iChar; break;
        case 'r': result.push_back('\r'); ++iChar; break;
        case 't': result.push_back('\t'); ++iChar; break;
        case 'v': result.push_back('\v'); ++iChar; break;
        case '\\': result.push_back('\\'); ++iChar; break;

        default:
          result.push_back(*iChar);
          break;
      }
    }
    else
    {
      result.push_back(*iChar);
    }
  }

  return result;
}


// When running a script (option -s/--script given), this returns the path
// of the script to run.
// Otherwise, it returns the text that should be displayed in the viewer.
std::string readInputOrScriptName(const cxxopts::ParseResult& args)
{
  if (args.count("input_file"))
  {
    // If an input file is specified, we load the entire file into
    // memory and return its content
    const auto& inputFilename = args["input_file"].as<std::string>();
    std::ifstream file(inputFilename, std::ios::ate);
    
    // If there was an error (file doesn't exist, we don't have permission,
    // other error etc.), return an empty string
    if (!file.is_open())
    {
      return {};
    }

    const auto fileSize = file.tellg();
    file.seekg(0);

    std::string inputText;
    inputText.resize(fileSize);
    file.read(&inputText[0], fileSize);

    return inputText;
  }
  else if (args.count("script_file"))
  {
    return args["script_file"].as<std::string>();
  }
  else
  {
    // If no input file is given, we return whatever was passed in
    // via the --message argument, but with escape sequences replaced
    return replaceEscapeSequences(args["message"].as<std::string>());
  }
}


// Returns the window title to display, based on the current options
std::string determineTitle(const cxxopts::ParseResult& args)
{
  if (args.count("title"))
  {
    return args["title"].as<std::string>();
  }
  else if (args.count("input_file"))
  {
    return args["input_file"].as<std::string>();
  }
  else if (args.count("error_display"))
  {
    return "Error!!";
  }
  else
  {
    return "Info";
  }
}


// This function implements the main loop
int run(SDL_Window* pWindow, const cxxopts::ParseResult& args)
{
  // Data structures and helper functions for dealing with controllers
  
  // List of all currently open controllers
  std::vector<SDL_GameController*> gameControllers;

  // Close all currently open controllers and clear the list
  auto clearGameControllers = [&]()
  {
    for (const auto pController : gameControllers)
    {
      SDL_GameControllerClose(pController);
    }

    gameControllers.clear();
  };

  // Look for game controllers currently plugged in, and try opening
  // them. This will open any controller that's recognized by SDL, i.e.
  // has a valid controller mapping.
  auto enumerateGameControllers = [&]()
  {
    clearGameControllers();

    for (std::uint8_t i = 0; i < SDL_NumJoysticks(); ++i) {
      if (SDL_IsGameController(i)) {
        gameControllers.push_back(SDL_GameControllerOpen(i));
      }
    }
  };


  // Create the view object. This is where all the core logic
  // is implemented. See view.hpp/view.cpp.
  // Ideally, all command line options should be converted to plain
  // C++ types before handing them over to the View, to
  // avoid making the View dependent on cxxopts.
  auto view = View{
    determineTitle(args),
    readInputOrScriptName(args),
    args.count("yes_button") > 0,
    args.count("wrap_lines") > 0,
    args.count("script_file") > 0};

  const auto& io = ImGui::GetIO();

  // Keep running until an exit code is set
  std::optional<int> exitCode;
  while (!exitCode)
  {
    // Process pending events
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
      // Forward events to Dear ImGui
      ImGui_ImplSDL2_ProcessEvent(&event);

      // Check if we need to quit, this directly handles some controller events.
      // Most controller events are handled by ImGui instead.
      if (
        event.type == SDL_QUIT ||
        (event.type == SDL_CONTROLLERBUTTONDOWN &&
         (event.cbutton.button == SDL_CONTROLLER_BUTTON_GUIDE || event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK)) ||
        (event.type == SDL_WINDOWEVENT &&
         event.window.event == SDL_WINDOWEVENT_CLOSE &&
         event.window.windowID == SDL_GetWindowID(pWindow))
      ) {
        return 0;
      }

      // Handle controller hot-plugging
      if (
        event.type == SDL_CONTROLLERDEVICEADDED ||
        event.type == SDL_CONTROLLERDEVICEREMOVED)
      {
        enumerateGameControllers();
      }
    }

    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame(pWindow, gameControllers);
    ImGui::NewFrame();

    // Draw the UI, respond to user input etc.
    exitCode = view.draw(io.DisplaySize);

    // Render and swap buffers to present the new frame
    ImGui::Render();

    glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
   
    SDL_GL_SwapWindow(pWindow);
  }

  return *exitCode;
}

}


int main(int argc, char** argv)
{
  const auto oArgs = parseArgs(argc, argv);
  if (!oArgs)
  {
    return -2;
  }

  const auto& args = *oArgs;


  // Read the SDL_GAMECONTROLLERCONFIG_FILE environment variable
  // and load the controller mapping database file that it points to,
  // if applicable.
  // This is done automatically by SDL starting with version 2.0.10,
  // but we want to backport the same behavior also to SDL 2.0.9,
  // hence this code.
  if (const auto dbFilePath = SDL_getenv("SDL_GAMECONTROLLERCONFIG_FILE"))
  {
    if (SDL_GameControllerAddMappingsFromFile(dbFilePath) >= 0)
    {
      std::cout << "Game controller mappings loaded\n";
    }
    else
    {
      std::cerr
        << "Could not load controller mappings from file '"
        << dbFilePath << "': " << SDL_GetError() << '\n';
    }
  }

  // Setup SDL
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0)
  {
    std::cerr << "Error: " << SDL_GetError() << '\n';
    return -1;
  }

  // Setup window and OpenGL
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

  SDL_DisplayMode displayMode;
  SDL_GetDesktopDisplayMode(0, &displayMode);

  auto pWindow = SDL_CreateWindow(
    "Log Viewer",
    SDL_WINDOWPOS_CENTERED,
    SDL_WINDOWPOS_CENTERED,
    displayMode.w,
    displayMode.h,
    SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_ALLOW_HIGHDPI);

  auto pGlContext = SDL_GL_CreateContext(pWindow);
  SDL_GL_MakeCurrent(pWindow, pGlContext);
  SDL_GL_SetSwapInterval(1); // Enable vsync

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  auto& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

  // Disable creation of imgui.ini
  io.IniFilename = nullptr;

  // Setup Dear ImGui style
  ImGui::StyleColorsDark();

  // Change the background to red if the --error_display option is given
  if (args.count("error_display")) {
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(ImColor(94, 11, 22, 255))); // Set window background to red
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(ImColor(94, 11, 22, 255)));
  }

  // Apply the requested font size 
  if (args.count("font_size"))
  {
    ImFontConfig config;
    config.SizePixels = args["font_size"].as<int>();
    ImGui::GetIO().Fonts->AddFontDefault(&config);
  }

  // Setup Platform/Renderer bindings
  ImGui_ImplSDL2_InitForOpenGL(pWindow, pGlContext);
  ImGui_ImplOpenGL3_Init(nullptr);

  // Main loop
  const auto exitCode = run(pWindow, args);

  // Cleanup
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();

  SDL_GL_DeleteContext(pGlContext);
  SDL_DestroyWindow(pWindow);
  SDL_Quit();

  return exitCode;
}
