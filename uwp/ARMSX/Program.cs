using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using SDL2;

namespace ARMSX
{
    class Program
    {
        [DllImport("libarmsx.dll", CallingConvention = CallingConvention.Cdecl, EntryPoint = "external_main")]
        private static extern int external_main(int argc, IntPtr argv, IntPtr external_window, IntPtr external_renderer);

        private static string[] launchArgs = Array.Empty<string>();

        static void Main(string[] args)
        {
            launchArgs = args ?? Array.Empty<string>();
            SDL.SDL_SetHint("SDL_WINRT_HANDLE_BACK_BUTTON", "1");
            SDL.SDL_main_func mainFunction = SDLMain;
            SDL.SDL_WinRTRunApp(mainFunction, IntPtr.Zero);
        }

        private static int SDLMain(int argc, IntPtr argv)
        {
            string[] nativeArgs = BuildNativeArgs();
            IntPtr[] argvPtrs = new IntPtr[nativeArgs.Length];
            IntPtr nativeArgv = IntPtr.Zero;

            try
            {
                for (int i = 0; i < nativeArgs.Length; i++)
                {
                    argvPtrs[i] = Marshal.StringToHGlobalAnsi(nativeArgs[i]);
                }

                nativeArgv = Marshal.AllocHGlobal(IntPtr.Size * nativeArgs.Length);
                for (int i = 0; i < nativeArgs.Length; i++)
                {
                    Marshal.WriteIntPtr(nativeArgv, i * IntPtr.Size, argvPtrs[i]);
                }

                int ret = external_main(nativeArgs.Length, nativeArgv, IntPtr.Zero, IntPtr.Zero);
                Debug.WriteLine($"external_main returned: {ret}");
                return ret;
            }
            finally
            {
                foreach (IntPtr ptr in argvPtrs)
                {
                    if (ptr != IntPtr.Zero)
                    {
                        Marshal.FreeHGlobal(ptr);
                    }
                }
                if (nativeArgv != IntPtr.Zero)
                {
                    Marshal.FreeHGlobal(nativeArgv);
                }
            }
        }

        private static string[] BuildNativeArgs()
        {
            string[] nativeArgs = new string[launchArgs.Length + 1];
            nativeArgs[0] = "armsx";
            Array.Copy(launchArgs, 0, nativeArgs, 1, launchArgs.Length);
            return nativeArgs;
        }

    }
}
