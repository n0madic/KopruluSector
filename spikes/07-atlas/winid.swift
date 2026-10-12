// Prints CGWindowIDs of on-screen windows whose owner name contains argv[1] (default dmengine).
// Used by capture_macos.sh to screenshot the spike window.
import CoreGraphics
import Foundation
let owner = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "dmengine"
let list = CGWindowListCopyWindowInfo([.optionOnScreenOnly], kCGNullWindowID) as! [[String: Any]]
for w in list {
  if let o = w[kCGWindowOwnerName as String] as? String, o.contains(owner) {
    print(w[kCGWindowNumber as String]!, o, w[kCGWindowName as String] ?? "")
  }
}
