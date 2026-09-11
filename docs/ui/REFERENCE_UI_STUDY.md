# HydraSeat UI reference study

## Scope and provenance

This pass studies three read-only reference repositories for interaction requirements and official Windows application structure. No reference source, algorithm, assets, constants, or wording were copied into HydraSeat.

- `splitscreenme-nucleus-master`: GPL-3.0 reference. Behavior and interaction concepts only.
- `WindowsAppSDK-Samples-main`: official Microsoft sample reference for C++/WinRT project and startup requirements.
- `WinUI-Gallery-main`: official Microsoft control and accessibility reference.

HydraSeat keeps its existing process/runtime, window placement, input isolation, recovery, Host protocol, Gate C, profile/catalog, and two-Seat authority unchanged.

## Nucleus UX patterns worth retaining as independent requirements

- Game selection is visually primary: a recognizable game row/card leads into setup instead of exposing runtime internals first.
- Input assignment is action-led. The screen tells a person to press a key, click a mouse button, or press a controller button and then shows the identified device. Hardware identifiers are not the task.
- The physical display regions are the assignment destinations. Input devices are grouped beneath the display where the resulting game will appear.
- Keyboard and mouse are presented as one person's usable input group even when Windows exposes multiple underlying collections.
- Readiness is progressive: the next action changes from setup to Next or Play, and a blocking state explains the one action required to continue.
- Unsupported/error states remain visible at the point of action instead of opening a developer console.

HydraSeat expresses these requirements in its own two-screen, two-Seat product model: Game -> Player 1 -> optional Player 2 -> Seat Setup -> Play.

## Nucleus areas that must not be copied

- GPL implementation code, class structure, drawing code, setup algorithms, device/runtime manipulation, input injection, window management, profiles, handlers, theme assets, cover assets, and wording.
- Nucleus runtime/core behavior, multi-instance orchestration, hooks, third-party integrations, or controller/input algorithms.
- Drag-only assignment. Drag may remain a shortcut, but HydraSeat requires selection plus explicit assign/unassign actions and intentional press/click identification.
- Nucleus concepts that conflate a player, input device, game instance, and screen. HydraSeat preserves separate Player, Seat hardware, Game, setup, and runtime-session authority.

## WinUI Gallery control mapping

If a future WinUI 3 shell is enabled, the minimum mapping is:

- two top-level destinations: a simple `Frame`/page switch or compact `NavigationView`; two destinations do not justify a deep navigation hierarchy;
- responsive structure: `Grid` with `VisualStateManager`/`AdaptiveTrigger` breakpoints;
- game library: `ListView` for selection, keyboard navigation, UI Automation, and modest installed-library sizes; `ItemsView` is an alternative only if cover-heavy layouts become a measured requirement; `ItemsRepeater` would require HydraSeat to recreate selection and accessibility behavior;
- game/player/display surfaces: card-like `Grid` containers using theme resources, not custom per-control paint code;
- Player selection: `ComboBox`, with an explicit localized `None (optional)` choice for Player 2;
- blocking or unsupported state: inline `InfoBar` adjacent to Play;
- `TeachingTip`: not part of the normal flow; use only for a genuinely new, dismissible one-time explanation;
- accessibility: headings/landmarks, explicit automation names for repeated Player/display groups, logical tab order, and shape/text status in addition to color;
- localization/theme: `.resw` resources and a small application `ResourceDictionary`; no Gallery-wide style copy.

## Windows App SDK project/startup pattern

The official unpackaged C++ sample uses a native `.vcxproj` with `UseWinUI`, `AppContainerApplication=false`, `AppxPackage=false`, and `WindowsPackageType=None`. It still imports three NuGet build packages: `Microsoft.WindowsAppSDK`, `Microsoft.Windows.CppWinRT`, and `Microsoft.Windows.SDK.BuildTools`. XAML compilation also adds generated module sources, IDL/WinMD/PRI output, and `App.xaml.cpp` creates and activates `MainWindow`. Native interop obtains `HWND` through `IWindowNative`; Win32 pixel sizing must apply the current DPI scale.

Packaged builds add an MSIX packaging project/manifest. Unpackaged does not mean dependency-free: the Windows App Runtime must be installed or the application must adopt an explicitly reviewed self-contained deployment. This is a separate deployment decision from HydraSeat's current CMake/MSVC executable graph.

## Win32 owner-draw versus WinUI 3

| Criterion | Keep current Win32 shell | Parallel WinUI 3 C++ shell |
| --- | --- | --- |
| Implementation complexity | Low for deleting legacy controls and preserving the current adapter; high if custom layout/painting keeps expanding | Higher initial project/XAML/IDL/generated-code boundary, lower long-term layout/control complexity |
| New dependency | None | Windows App SDK, C++/WinRT, SDK build tools, Windows App Runtime |
| Deploy/install | Existing executable contract | Runtime/framework or reviewed self-contained deployment plus optional MSIX work |
| Unpackaged | Already supported | Supported, but still requires package restore and runtime deployment/bootstrap |
| Windows 10 | Existing Win32 SDK behavior | Sample minimum is Windows 10 1809; exact supported App SDK release must be pinned and qualified |
| x64/x86 | Existing CMake/MSVC matrix | Both configurations are possible, but both need restored packages and runtime artifacts |
| Localization/DPI | Manual measurement and layout contracts | Native XAML resource/layout behavior is substantially better |
| Accessibility | Manual focus/UIA/system-color work | Built-in control peers and keyboard semantics reduce custom work |
| Maintenance | Acceptable only if the shell is simplified and owner-draw growth stops | Better after migration, at the cost of a second build/deploy stack |
| Existing C++ model connection | Direct | Straightforward thin C++ adapter, but generated WinRT types must not leak into runtime/core |
| Source growth / AI-slop risk | Lowest for this pass; risk rises if hidden compatibility branches remain | Larger initial scaffold and a strong risk of copying Gallery/sample ceremony without product need |

### Decision

Keep the Win32 shell for this pass and simplify it. The installed environment has MSVC Build Tools 17.14 and Windows SDK 10.0.26100, but no installed `Microsoft.WindowsAppSDK` or `Microsoft.Windows.CppWinRT` NuGet packages/build imports. The official unpackaged sample fails its build intentionally when those imports are missing. The task forbids arbitrary package installation, vendoring, and system changes, so a buildable WinUI vertical slice cannot be produced safely here.

This is not an endorsement of continued owner-draw expansion. The implementation removes hidden Seat-binding/advanced controls and normal-path diagnostics, retains standard native controls where possible, and keeps localization-driven layout as one bounded shell concern.

## Migration boundary

The replaceable boundary is `launcher_ui::showLauncherWindow` plus `LauncherUiModel` and the existing Host activation callback. A future WinUI executable/page may:

1. receive the same bounded `SeatConfigDocument` and `GameRuntimeRequirement` projection;
2. call the same `LauncherUiModel` selection/preview operations through a thin C++ adapter;
3. invoke the existing `LauncherActivate` callback only for the exact compiled immutable plan;
4. return `OpenHardwareSetup` to the existing hardware screen.

It must not own Host authority, recreate process/window/input/recovery logic, persist runtime handles, or introduce a manager/service layer. The current Win32 shell remains present until a future build environment supplies pinned Windows App SDK prerequisites and x64/x86 deployment validation.

## Files consulted

Nucleus behavioral reference:

- `Master/NucleusGaming/Controls/GameControl.cs`
- `Master/NucleusGaming/Controls/SetupScreen/SetupScreenControl.cs`
- `Master/NucleusGaming/Controls/SetupScreen/DevicesFunctions.cs`
- `Master/NucleusGaming/Controls/SetupScreen/Draw.cs`
- `Master/NucleusGaming/Controls/UserInputControl.cs`
- `Master/NucleusCoopTool/Tools/InputsText.cs`
- `Master/NucleusCoopTool/UI/UI_Actions.cs`

Windows App SDK structure:

- `Samples/CustomControls/CppAppUnpackaged/packages.config`
- `Samples/CustomControls/CppAppUnpackaged/CppAppUnpackaged.vcxproj`
- `Samples/CustomControls/CppAppUnpackaged/App.xaml.cpp`
- `Samples/CustomControls/CppAppUnpackaged/MainWindow.xaml`
- `Samples/CustomControls/CppAppUnpackaged/MainWindow.xaml.cpp`
- `Templates/VSIX/Project Templates/cpp-winui-template/WinUI3TemplateCpp.vcxproj`
- `Templates/VSIX/Project Templates/cpp-winui-template/App.xaml.cpp`
- `DynamicDependenciesSample/DynamicDependencies/README.md`

WinUI Gallery controls/accessibility:

- `Samples/NavigationView/NavigationViewPage.xaml`
- `Samples/Grid/GridPage.xaml`
- `Samples/ItemsView/ItemsViewPage.xaml`
- `Samples/ListView/ListViewPage.xaml`
- `Samples/ComboBox/ComboBoxPage.xaml`
- `Samples/InfoBar/InfoBarPage.xaml`
- `Samples/AccessibilityKeyboard/AccessibilityKeyboardPage.xaml`
- `Samples/AccessibilityScreenReader/AccessibilityScreenReaderPage.xaml`
- `Styles/ItemTemplates.xaml`
- `Styles/Grid.xaml`
