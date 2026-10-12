// Prints the CGWindowID of a process's main on-screen window (default owner: dmengine),
// for use with `screencapture -l <id>`, which captures the window even when covered.
// Usage: swift tools/macos/window_id.swift [owner]
import CoreGraphics
import Foundation

let owner = CommandLine.arguments.count >= 2 ? CommandLine.arguments[1] : "dmengine"
let list = CGWindowListCopyWindowInfo([.optionOnScreenOnly], kCGNullWindowID) as? [[String: Any]] ?? []
func area(_ w: [String: Any]) -> Double {
    let b = w[kCGWindowBounds as String] as? [String: Double] ?? [:]
    return (b["Width"] ?? 0) * (b["Height"] ?? 0)
}
// The largest normal-layer window of the owner (engines may also own helper windows).
guard let info = list.filter({ ($0[kCGWindowOwnerName as String] as? String) == owner && ($0[kCGWindowLayer as String] as? Int) == 0 })
        .max(by: { area($0) < area($1) }),
      let id = info[kCGWindowNumber as String] as? CGWindowID else {
    FileHandle.standardError.write("no window for \(owner)\n".data(using: .utf8)!)
    exit(1)
}
print(id)
