using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using SDL2;
using Windows.Storage;
using Windows.Storage.Pickers;

namespace WindowsHostSDL2
{
    class Program
    {
        [DllImport("libarmsx.dll", CallingConvention = CallingConvention.Cdecl, EntryPoint = "external_main")]
        private static extern int external_main(int argc, IntPtr argv, IntPtr external_window, IntPtr external_renderer);

        static void Main(string[] args)
        {
            SDL.SDL_SetHint("SDL_WINRT_HANDLE_BACK_BUTTON", "1");
            SDL.SDL_main_func mainFunction = SDLMain;
            SDL.SDL_WinRTRunApp(mainFunction, IntPtr.Zero);
        }

        private static int SDLMain(int argc, IntPtr argv)
        {
            var localFolder = ApplicationData.Current.LocalFolder;
            Debug.WriteLine($"LocalState folder path: {localFolder.Path}");


            uint initFlags = SDL.SDL_INIT_VIDEO | SDL.SDL_INIT_AUDIO | SDL.SDL_INIT_JOYSTICK | SDL.SDL_INIT_GAMECONTROLLER;
            if (SDL.SDL_Init(initFlags) != 0)
            {
                Debug.WriteLine("SDL Initialization failed: " + SDL.SDL_GetError());
                return -1;
            }



            IntPtr window = SDL.SDL_CreateWindow(
                "Windows Host SDL2 App",
                SDL.SDL_WINDOWPOS_CENTERED,
                SDL.SDL_WINDOWPOS_CENTERED,
                1280,
                720,
                SDL.SDL_WindowFlags.SDL_WINDOW_OPENGL | SDL.SDL_WindowFlags.SDL_WINDOW_SHOWN
            );
            if (window == IntPtr.Zero)
            {
                Debug.WriteLine("Window creation failed: " + SDL.SDL_GetError());
                SDL.SDL_Quit();
                return -1;
            }

            // Create an SDL renderer (host-provided renderer expected by the DLL)
            IntPtr renderer = SDL.SDL_CreateRenderer(window, -1, (int)(SDL.SDL_RendererFlags.SDL_RENDERER_ACCELERATED | SDL.SDL_RendererFlags.SDL_RENDERER_PRESENTVSYNC));
            if (renderer == IntPtr.Zero)
            {
                Debug.WriteLine("Renderer creation failed: " + SDL.SDL_GetError());
                SDL.SDL_DestroyWindow(window);
                SDL.SDL_Quit();
                return -1;
            }

            string[] arguments = { "ChonkyStation3UWP", "", localFolder.Path };
            int newArgc = arguments.Length;
            IntPtr[] argvPtrs = new IntPtr[newArgc];
            for (int i = 0; i < newArgc; i++)
            {
                argvPtrs[i] = Marshal.StringToHGlobalAnsi(arguments[i]);
            }

            IntPtr newArgv = Marshal.AllocHGlobal(IntPtr.Size * newArgc);
            for (int i = 0; i < newArgc; i++)
            {
                Marshal.WriteIntPtr(newArgv, i * IntPtr.Size, argvPtrs[i]);
            }

            int ret = external_main(newArgc, newArgv, window, renderer);
            Debug.WriteLine($"external_main returned: {ret}");

            for (int i = 0; i < newArgc; i++)
            {
                Marshal.FreeHGlobal(argvPtrs[i]);
            }
            Marshal.FreeHGlobal(newArgv);

            SDL.SDL_DestroyRenderer(renderer);
            SDL.SDL_DestroyWindow(window);
            SDL.SDL_Quit();

            return ret;
        }


    }
}