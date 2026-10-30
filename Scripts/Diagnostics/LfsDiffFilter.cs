// Repository-local adapter: hash-only LFS clean for read-only Git; native LFS for writes.
// No vendor executable is replaced. Compile with the Windows .NET Framework CSC.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Management;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Threading.Tasks;

internal static class LfsDiffFilter
{
    private static string nativeLfs;
    private static string nativeGit;
    private static string invocationKind = "native-other";
    private const int PointerLimit = 1024;

    [DllImport("shell32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern IntPtr CommandLineToArgvW(string commandLine, out int count);
    [DllImport("kernel32.dll")]
    private static extern IntPtr LocalFree(IntPtr memory);

    private static int Main(string[] arguments)
    {
        try
        {
            bool testHashOnly = arguments.Length == 5 && arguments[4] == "--test-hash-only";
            if ((arguments.Length != 4 && !testHashOnly) || arguments[0] != "--native" || arguments[2] != "--git")
                throw new ArgumentException("Expected --native <git-lfs.exe> --git <git.exe>.");
            nativeLfs = Path.GetFullPath(arguments[1]);
            nativeGit = Path.GetFullPath(arguments[3]);
            if (!File.Exists(nativeLfs) || !File.Exists(nativeGit))
                throw new FileNotFoundException("The original Git/Git LFS executable is missing.");

            string gitAncestor = FindGitAncestor();
            if (testHashOnly && gitAncestor != null)
                throw new InvalidOperationException("The protocol-test entry point cannot be used as a Git filter.");
            bool readOnlyCommand = testHashOnly || (!String.IsNullOrEmpty(gitAncestor) && IsReadOnlyCommand(gitAncestor));
            bool standardConfiguration = readOnlyCommand && HasStandardLfsConfiguration();
            bool hashOnly = readOnlyCommand && standardConfiguration;
            if (Environment.GetEnvironmentVariable("CODEX_LFS_DIFF_TRACE") == "1")
                Console.Error.WriteLine("lfs-diff-filter: " + (hashOnly ? "hash-only" : "native") + " command=" + invocationKind + " standard-lfs=" + standardConfiguration);
            if (!hashOnly) return RunNativeFilter();
            RunHashOnlyFilter(Console.OpenStandardInput(), Console.OpenStandardOutput());
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine("lfs-diff-filter: " + error.Message);
            return 1;
        }
    }

    private static string[] ParseArguments(string commandLine)
    {
        int count;
        IntPtr values = CommandLineToArgvW(commandLine, out count);
        if (values == IntPtr.Zero) throw new InvalidOperationException("Cannot parse Git command line.");
        try
        {
            string[] result = new string[count];
            for (int index = 0; index < count; index++)
                result[index] = Marshal.PtrToStringUni(Marshal.ReadIntPtr(values, index * IntPtr.Size));
            return result;
        }
        finally { LocalFree(values); }
    }

    private static bool IsReadOnlyCommand(string commandLine)
    {
        string[] values = ParseArguments(commandLine);
        for (int index = 1; index < values.Length; index++)
        {
            string value = values[index];
            if (value == "-c" || value == "-C" || value == "--git-dir" || value == "--work-tree" ||
                value == "--namespace" || value == "--exec-path" || value == "--config-env")
            {
                if (++index >= values.Length) return false;
                continue;
            }
            if (value.StartsWith("--git-dir=") || value.StartsWith("--work-tree=") ||
                value.StartsWith("--namespace=") || value.StartsWith("--exec-path=") ||
                value.StartsWith("--config-env=")) continue;
            if (value == "-p" || value == "--paginate" || value == "-P" || value == "--no-pager" ||
                value == "--no-replace-objects" || value == "--literal-pathspecs" ||
                value == "--glob-pathspecs" || value == "--noglob-pathspecs" ||
                value == "--icase-pathspecs" || value == "--no-optional-locks" ||
                value == "--no-lazy-fetch") continue;
            // Unknown options and aliases fail closed to the original filter.
            if (value == "diff" || value == "status") { invocationKind = value; return true; }
            if (value == "hash-object")
            {
                invocationKind = "hash-object";
                bool pathsFromStdin = false;
                for (int option = index + 1; option < values.Length; option++)
                {
                    if (values[option] == "--") break;
                    if (values[option] == "--stdin" || values[option] == "--path" ||
                        values[option].StartsWith("--path=")) return false;
                    if (values[option] == "--stdin-paths") pathsFromStdin = true;
                    if (values[option].StartsWith("-") && !values[option].StartsWith("--") &&
                        values[option].IndexOf('w') >= 0) { invocationKind = "hash-object-write"; return false; }
                }
                return pathsFromStdin;
            }
            return false;
        }
        return false;
    }

    private static string FindGitAncestor()
    {
        try
        {
            int processId = Process.GetCurrentProcess().Id;
            HashSet<int> visited = new HashSet<int>();
            for (int depth = 0; depth < 16 && visited.Add(processId); depth++)
            {
                using (ManagementObjectSearcher query = new ManagementObjectSearcher(
                    "SELECT Name,CommandLine,ParentProcessId FROM Win32_Process WHERE ProcessId=" + processId))
                using (ManagementObjectCollection results = query.Get())
                {
                    bool found = false;
                    foreach (ManagementObject item in results)
                    {
                        using (item)
                        {
                            found = true;
                            string name = Convert.ToString(item["Name"]);
                            string commandLine = Convert.ToString(item["CommandLine"]);
                            if (String.Equals(name, "git.exe", StringComparison.OrdinalIgnoreCase))
                                return commandLine;
                            processId = Convert.ToInt32(item["ParentProcessId"]);
                        }
                        break;
                    }
                    if (!found || processId == 0) return null;
                }
            }
        }
        catch { /* Inaccessible ancestry must retain the original LFS behavior. */ return String.Empty; }
        return String.Empty;
    }

    private static Process StartChild(string executable, string arguments)
    {
        ProcessStartInfo start = new ProcessStartInfo(executable, arguments);
        start.UseShellExecute = false;
        start.CreateNoWindow = true;
        start.RedirectStandardInput = true;
        start.RedirectStandardOutput = true;
        start.RedirectStandardError = true;
        return Process.Start(start);
    }

    private static bool HasStandardLfsConfiguration()
    {
        try
        {
            using (Process child = StartChild(nativeGit,
                "config --get-regexp \"^(lfs\\.extension\\.|diff\\.lfs\\.(command|textconv)$)\""))
            {
                child.StandardInput.Close();
                string output = child.StandardOutput.ReadToEnd();
                string errors = child.StandardError.ReadToEnd();
                child.WaitForExit();
                // Git returns 1 when no configuration keys match.
                return child.ExitCode == 1 && output.Length == 0 && errors.Length == 0;
            }
        }
        catch { return false; }
    }

    private static int RunNativeFilter()
    {
        using (Process child = StartChild(nativeLfs, "filter-process"))
        {
            Stream input = Console.OpenStandardInput();
            Stream output = Console.OpenStandardOutput();
            Stream errors = Console.OpenStandardError();
            Task.Factory.StartNew(delegate
            {
                try { Pump(input, child.StandardInput.BaseStream); }
                catch (IOException) { }
                catch (ObjectDisposedException) { }
                finally { try { child.StandardInput.Close(); } catch { } }
            }, TaskCreationOptions.LongRunning);
            Task stdout = Task.Factory.StartNew(delegate { Pump(child.StandardOutput.BaseStream, output); }, TaskCreationOptions.LongRunning);
            Task stderr = Task.Factory.StartNew(delegate { Pump(child.StandardError.BaseStream, errors); }, TaskCreationOptions.LongRunning);
            child.WaitForExit();
            Task.WaitAll(stdout, stderr);
            output.Flush();
            errors.Flush();
            return child.ExitCode;
        }
    }

    private static void Pump(Stream input, Stream output)
    {
        byte[] buffer = new byte[65536];
        int count;
        while ((count = input.Read(buffer, 0, buffer.Length)) > 0)
        {
            output.Write(buffer, 0, count);
            output.Flush(); // A filter handshake must not wait for a full buffer.
        }
    }

    private static byte[] ReadPacket(Stream input)
    {
        byte[] header = new byte[4];
        int read = input.Read(header, 0, 4);
        if (read == 0) return null;
        ReadRemaining(input, header, read, 4 - read);
        int length = Int32.Parse(Encoding.ASCII.GetString(header), NumberStyles.HexNumber, CultureInfo.InvariantCulture);
        if (length == 0) return new byte[0];
        if (length < 4 || length > 65520) throw new InvalidDataException("Invalid filter packet size.");
        byte[] result = new byte[length - 4];
        ReadRemaining(input, result, 0, result.Length);
        return result;
    }

    private static void ReadRemaining(Stream input, byte[] buffer, int offset, int length)
    {
        while (length > 0)
        {
            int read = input.Read(buffer, offset, length);
            if (read == 0) throw new EndOfStreamException("Incomplete filter packet.");
            offset += read;
            length -= read;
        }
    }

    private static List<string> ReadHeaders(Stream input, bool allowEnd)
    {
        List<string> result = new List<string>();
        while (true)
        {
            byte[] packet = ReadPacket(input);
            if (packet == null)
            {
                if (allowEnd && result.Count == 0) return null;
                throw new EndOfStreamException("Incomplete filter headers.");
            }
            if (packet.Length == 0) return result;
            if (result.Count >= 64) throw new InvalidDataException("Too many filter headers.");
            result.Add(Encoding.UTF8.GetString(packet).TrimEnd('\n'));
        }
    }

    private static void WritePacket(Stream output, byte[] payload)
    {
        byte[] header = Encoding.ASCII.GetBytes((payload.Length + 4).ToString("x4", CultureInfo.InvariantCulture));
        output.Write(header, 0, header.Length);
        output.Write(payload, 0, payload.Length);
    }

    private static void WriteText(Stream output, string value) { WritePacket(output, Encoding.UTF8.GetBytes(value)); }
    private static void FlushPackets(Stream output)
    {
        byte[] flush = Encoding.ASCII.GetBytes("0000");
        output.Write(flush, 0, flush.Length);
    }

    private static bool IsExistingPointer(byte[] content)
    {
        using (Process child = StartChild(nativeLfs, "pointer --check --stdin"))
        {
            child.StandardInput.BaseStream.Write(content, 0, content.Length);
            child.StandardInput.Close();
            string output = child.StandardOutput.ReadToEnd();
            string errors = child.StandardError.ReadToEnd();
            child.WaitForExit();
            if (child.ExitCode != 0 && child.ExitCode != 1)
                throw new InvalidDataException("Native LFS pointer validation failed: " + errors);
            return child.ExitCode == 0;
        }
    }

    private static void RunHashOnlyFilter(Stream input, Stream output)
    {
        List<string> hello = ReadHeaders(input, false);
        if (!hello.Contains("git-filter-client") || !hello.Contains("version=2"))
            throw new InvalidDataException("Unsupported filter protocol.");
        WriteText(output, "git-filter-server\n");
        WriteText(output, "version=2\n");
        FlushPackets(output);
        output.Flush();
        List<string> capabilities = ReadHeaders(input, false);
        if (!capabilities.Contains("capability=clean")) throw new InvalidDataException("Clean capability not requested.");
        WriteText(output, "capability=clean\n");
        FlushPackets(output);
        output.Flush();

        List<string> headers;
        while ((headers = ReadHeaders(input, true)) != null)
        {
            if (!headers.Contains("command=clean")) throw new InvalidDataException("Unexpected command in read-only diff.");
            long size = 0;
            MemoryStream prefix = new MemoryStream();
            byte[] digest;
            using (SHA256 hash = SHA256.Create())
            {
                while (true)
                {
                    byte[] packet = ReadPacket(input);
                    if (packet == null) throw new EndOfStreamException("Incomplete clean content.");
                    if (packet.Length == 0) break;
                    hash.TransformBlock(packet, 0, packet.Length, packet, 0);
                    if (prefix.Length < PointerLimit)
                        prefix.Write(packet, 0, Math.Min(packet.Length, PointerLimit - (int)prefix.Length));
                    size = checked(size + packet.Length);
                }
                hash.TransformFinalBlock(new byte[0], 0, 0);
                digest = hash.Hash;
            }
            byte[] response;
            if (size == 0) response = new byte[0]; // Native LFS leaves empty files empty.
            else if (size <= PointerLimit && IsExistingPointer(prefix.ToArray())) response = prefix.ToArray();
            else
            {
                string oid = BitConverter.ToString(digest).Replace("-", "").ToLowerInvariant();
                response = Encoding.ASCII.GetBytes("version https://git-lfs.github.com/spec/v1\noid sha256:" + oid + "\nsize " + size.ToString(CultureInfo.InvariantCulture) + "\n");
            }
            WriteText(output, "status=success\n");
            FlushPackets(output);
            if (response.Length != 0) WritePacket(output, response);
            FlushPackets(output);
            FlushPackets(output);
            output.Flush();
        }
    }
}
