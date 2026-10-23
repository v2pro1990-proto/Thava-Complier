using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;
using Microsoft.Build.Framework;
using Microsoft.Build.Utilities;

namespace Thava.Build.Tasks
{
    public class ThavaCompileTask : Task
    {
        [Required]
        public ITaskItem[] Sources { get; set; }

        public string ProjectDir { get; set; }

        public string DefaultOutputFile { get; set; }

        public override bool Execute()
        {
            if (Sources == null || Sources.Length == 0)
            {
                return true;
            }

            try
            {
                // 1. Collect all non-excluded source files
                List<ITaskItem> candidates = new List<ITaskItem>();
                Dictionary<string, ITaskItem> itemByFullPath = new Dictionary<string, ITaskItem>(StringComparer.OrdinalIgnoreCase);
                Dictionary<string, string> fileByName = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
                HashSet<string> allFullPaths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

                foreach (ITaskItem item in Sources)
                {
                    string excluded = item.GetMetadata("ExcludedFromBuild");
                    if (string.Equals(excluded, "true", StringComparison.OrdinalIgnoreCase))
                        continue;

                    string fullPath = item.GetMetadata("FullPath");
                    if (string.IsNullOrEmpty(fullPath))
                        fullPath = Path.GetFullPath(item.ItemSpec);

                    if (!File.Exists(fullPath))
                    {
                        Log.LogError("Thava", "THV0001", null, fullPath, 1, 1, 0, 0, "Source file not found: " + fullPath);
                        return false;
                    }

                    if (!allFullPaths.Contains(fullPath))
                    {
                        candidates.Add(item);
                        allFullPaths.Add(fullPath);
                        itemByFullPath[fullPath] = item;
                        fileByName[Path.GetFileName(fullPath)] = fullPath;
                    }
                }

                if (candidates.Count == 0)
                    return true;

                // 2. Scan all candidate .thv files for @attach directives to build the dependency graph
                HashSet<string> attachedModules = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                Dictionary<string, List<string>> dependencies = new Dictionary<string, List<string>>(StringComparer.OrdinalIgnoreCase);
                Regex attachRegex = new Regex(@"^\s*@attach\s+(?:""([^""]+)""|<([^>]+)>)", RegexOptions.Multiline);

                foreach (ITaskItem item in candidates)
                {
                    string fullPath = item.GetMetadata("FullPath");
                    string dir = Path.GetDirectoryName(fullPath);
                    string content = File.ReadAllText(fullPath, Encoding.UTF8);

                    List<string> deps = new List<string>();
                    dependencies[fullPath] = deps;

                    foreach (Match m in attachRegex.Matches(content))
                    {
                        string rawTarget = m.Groups[1].Success ? m.Groups[1].Value : m.Groups[2].Value;
                        if (string.IsNullOrWhiteSpace(rawTarget))
                            continue;

                        string resolved = Path.GetFullPath(Path.Combine(dir, rawTarget));
                        if (allFullPaths.Contains(resolved))
                        {
                            attachedModules.Add(resolved);
                            deps.Add(resolved);
                        }
                        else
                        {
                            string justName = Path.GetFileName(rawTarget);
                            string matchedFull;
                            if (fileByName.TryGetValue(justName, out matchedFull))
                            {
                                attachedModules.Add(matchedFull);
                                deps.Add(matchedFull);
                            }
                        }
                    }
                }

                // 3. Filter root entry points vs attached modules
                List<ITaskItem> entryPoints = new List<ITaskItem>();
                foreach (ITaskItem item in candidates)
                {
                    string fullPath = item.GetMetadata("FullPath");
                    string role = item.GetMetadata("EntryPointRole");
                    if (string.IsNullOrEmpty(role))
                        role = "Auto";

                    if (string.Equals(role, "Module", StringComparison.OrdinalIgnoreCase))
                    {
                        Log.LogMessage(MessageImportance.High, "  [ThavaBuild] Skipping module '{0}' (EntryPointRole=Module)", Path.GetFileName(fullPath));
                        continue;
                    }

                    if (string.Equals(role, "EntryPoint", StringComparison.OrdinalIgnoreCase))
                    {
                        entryPoints.Add(item);
                        continue;
                    }

                    // Auto mode: skip standalone compilation if another file in the project attaches this file via @attach
                    if (attachedModules.Contains(fullPath) && candidates.Count > 1)
                    {
                        Log.LogMessage(MessageImportance.High, "  [ThavaBuild] Module '{0}' is attached via @attach (resolved in dependency graph)", Path.GetFileName(fullPath));
                        continue;
                    }

                    entryPoints.Add(item);
                }

                // Fallback: if all files attached each other cyclically, use the first candidate
                if (entryPoints.Count == 0 && candidates.Count > 0)
                {
                    entryPoints.Add(candidates[0]);
                }

                // 4. Group entry points by their target OutputFile so multi-file projects (especially .thdl dynamic libraries)
                //    compile into a single unified binary without overwriting each other.
                Dictionary<string, List<ITaskItem>> targetGroups = new Dictionary<string, List<ITaskItem>>(StringComparer.OrdinalIgnoreCase);
                List<string> targetOrder = new List<string>();

                foreach (ITaskItem entry in entryPoints)
                {
                    string outputFile = entry.GetMetadata("OutputFile");
                    if (string.IsNullOrEmpty(outputFile))
                        outputFile = DefaultOutputFile;

                    string fullOutput = Path.GetFullPath(outputFile);
                    List<ITaskItem> group;
                    if (!targetGroups.TryGetValue(fullOutput, out group))
                    {
                        group = new List<ITaskItem>();
                        targetGroups[fullOutput] = group;
                        targetOrder.Add(fullOutput);
                    }
                    group.Add(entry);
                }

                foreach (string fullOutput in targetOrder)
                {
                    List<ITaskItem> group = targetGroups[fullOutput];
                    if (!CompileTargetGroup(group, fullOutput))
                    {
                        return false;
                    }
                }

                return !Log.HasLoggedErrors;
            }
            catch (Exception ex)
            {
                Log.LogError("Thava", "THV9000", null, null, 1, 1, 0, 0,
                    "Unexpected error in ThavaCompileTask: " + ex.Message);
                return false;
            }
        }

        private bool CompileTargetGroup(List<ITaskItem> entries, string fullOutput)
        {
            ITaskItem primary = entries[0];
            string primarySource = primary.GetMetadata("FullPath");

            string compilerExe = primary.GetMetadata("CompilerExecutable");
            if (string.IsNullOrEmpty(compilerExe))
                compilerExe = "thavac.exe";

            string backendMode = primary.GetMetadata("BackendMode");
            string outputBuildType = primary.GetMetadata("OutputBuildType");
            string optLevel = primary.GetMetadata("OptimizationLevel");
            string hostCpp = primary.GetMetadata("HostCppCompiler");
            string extraOpts = primary.GetMetadata("AdditionalOptions");

            string outDir = Path.GetDirectoryName(fullOutput);
            if (!string.IsNullOrEmpty(outDir) && !Directory.Exists(outDir))
            {
                Directory.CreateDirectory(outDir);
            }

            StringBuilder args = new StringBuilder();
            List<string> fileNames = new List<string>();
            foreach (ITaskItem item in entries)
            {
                string src = item.GetMetadata("FullPath");
                if (args.Length > 0)
                    args.Append(" ");
                args.AppendFormat("\"{0}\"", src);
                fileNames.Add(Path.GetFileName(src));
            }

            args.AppendFormat(" -o \"{0}\"", fullOutput);

            if (!string.IsNullOrEmpty(optLevel))
            {
                args.AppendFormat(" -{0}", optLevel.Trim());
            }

            if (!string.IsNullOrEmpty(backendMode))
            {
                string mode = backendMode.Trim();
                if (string.Equals(mode, "CppTranspiler", StringComparison.OrdinalIgnoreCase))
                    args.Append(" --cpp");
                else if (string.Equals(mode, "WinArm64", StringComparison.OrdinalIgnoreCase))
                    args.Append(" --target win-arm64");
                else if (string.Equals(mode, "LinuxElf64", StringComparison.OrdinalIgnoreCase))
                    args.Append(" --backend elf");
                else if (string.Equals(mode, "LinuxArm64", StringComparison.OrdinalIgnoreCase))
                    args.Append(" --arm64");
                else
                    args.Append(" --native");
            }

            if (string.Equals(outputBuildType, "DynamicLibrary", StringComparison.OrdinalIgnoreCase))
            {
                args.Append(" --lib");
            }

            if (!string.IsNullOrEmpty(hostCpp) &&
                !string.Equals(hostCpp.Trim(), "Auto", StringComparison.OrdinalIgnoreCase))
            {
                args.AppendFormat(" --cxx \"{0}\"", hostCpp.Trim());
            }

            if (!string.IsNullOrEmpty(extraOpts))
            {
                args.AppendFormat(" {0}", extraOpts.Trim());
            }

            Log.LogMessage(MessageImportance.High, "  [ThavaBuild] {0} -> {1}", string.Join(" + ", fileNames.ToArray()), fullOutput);
            Log.LogMessage(MessageImportance.Normal, "  Command: \"{0}\" {1}", compilerExe, args.ToString());

            string workDir = !string.IsNullOrEmpty(ProjectDir) && Directory.Exists(ProjectDir)
                ? ProjectDir
                : Path.GetDirectoryName(primarySource);

            ProcessStartInfo psi = new ProcessStartInfo
            {
                FileName = compilerExe,
                Arguments = args.ToString(),
                WorkingDirectory = workDir,
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true,
                StandardOutputEncoding = Encoding.UTF8,
                StandardErrorEncoding = Encoding.UTF8
            };

            bool loggedError = false;
            Regex lineRegex = new Regex(@"(?:at line\s+|\(Line\s+|line\s+)(\d+)", RegexOptions.IgnoreCase);

            Action<string, bool> processLine = (line, isStdErr) =>
            {
                if (string.IsNullOrWhiteSpace(line))
                    return;

                string trimmed = line.Trim();
                int lineNum = 1;
                Match m = lineRegex.Match(trimmed);
                if (m.Success)
                {
                    int.TryParse(m.Groups[1].Value, out lineNum);
                    if (lineNum < 1) lineNum = 1;
                }

                bool isError = isStdErr ||
                               trimmed.IndexOf("Error", StringComparison.OrdinalIgnoreCase) >= 0 ||
                               trimmed.IndexOf("Exception", StringComparison.OrdinalIgnoreCase) >= 0;
                bool isWarning = trimmed.IndexOf("Warning", StringComparison.OrdinalIgnoreCase) >= 0;

                if (isWarning && !trimmed.StartsWith("Error", StringComparison.OrdinalIgnoreCase))
                {
                    Log.LogWarning("Thava", "THV0100", null, primarySource, lineNum, 1, 0, 0, trimmed);
                }
                else if (isError)
                {
                    string code = "THV4001";
                    if (trimmed.IndexOf("Lexer Error", StringComparison.OrdinalIgnoreCase) >= 0)
                        code = "THV1001";
                    else if (trimmed.IndexOf("Parser Error", StringComparison.OrdinalIgnoreCase) >= 0 ||
                             trimmed.IndexOf("Syntax Error", StringComparison.OrdinalIgnoreCase) >= 0)
                        code = "THV2001";
                    else if (trimmed.IndexOf("Static Type Error", StringComparison.OrdinalIgnoreCase) >= 0)
                        code = "THV3001";

                    Log.LogError("Thava", code, null, primarySource, lineNum, 1, 0, 0, trimmed);
                    loggedError = true;
                }
                else
                {
                    Log.LogMessage(MessageImportance.High, "    " + trimmed);
                }
            };

            using (Process proc = new Process())
            {
                proc.StartInfo = psi;
                proc.Start();

                string stdOut = proc.StandardOutput.ReadToEnd();
                string stdErr = proc.StandardError.ReadToEnd();
                proc.WaitForExit();

                if (!string.IsNullOrEmpty(stdOut))
                {
                    foreach (string line in stdOut.Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries))
                    {
                        processLine(line, false);
                    }
                }

                if (!string.IsNullOrEmpty(stdErr))
                {
                    foreach (string line in stdErr.Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries))
                    {
                        processLine(line, true);
                    }
                }

                if (proc.ExitCode != 0 && !loggedError)
                {
                    Log.LogError("Thava", "THV4000", null, primarySource, 1, 1, 0, 0,
                        string.Format("thavac.exe exited with code {0} while compiling '{1}'.", proc.ExitCode, string.Join(", ", fileNames.ToArray())));
                    return false;
                }

                return proc.ExitCode == 0 && !loggedError;
            }
        }
    }
}
