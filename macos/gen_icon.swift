import AppKit

let outDir = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "build"
let iconsetPath = "\(outDir)/AppIcon.iconset"
try FileManager.default.createDirectory(atPath: iconsetPath, withIntermediateDirectories: true)

let entries: [(String, Int)] = [
    ("icon_16x16", 16), ("icon_16x16@2x", 32),
    ("icon_32x32", 32), ("icon_32x32@2x", 64),
    ("icon_128x128", 128), ("icon_128x128@2x", 256),
    ("icon_256x256", 256), ("icon_256x256@2x", 512),
    ("icon_512x512", 512), ("icon_512x512@2x", 1024),
]

let phosphor = NSColor(red: 0.25, green: 1.0, blue: 0.45, alpha: 1)

for (name, px) in entries {
    let s = CGFloat(px)
    let img = NSImage(size: NSSize(width: s, height: s))
    img.lockFocus()

    // VT420 putty-colored case
    let inset = s * 0.06
    let caseRect = NSRect(x: inset, y: inset, width: s - inset * 2, height: s - inset * 2)
    let caseBg = NSBezierPath(roundedRect: caseRect, xRadius: s * 0.16, yRadius: s * 0.16)
    NSGradient(
        starting: NSColor(red: 0.88, green: 0.86, blue: 0.80, alpha: 1),
        ending: NSColor(red: 0.72, green: 0.70, blue: 0.64, alpha: 1)
    )?.draw(in: caseBg, angle: -90)

    // dark CRT glass, slightly high in the case
    let sx = s * 0.16, syTop = s * 0.16, syBot = s * 0.24
    let screenRect = NSRect(x: sx, y: syBot, width: s - sx * 2, height: s - syTop - syBot)
    let screen = NSBezierPath(roundedRect: screenRect, xRadius: s * 0.05, yRadius: s * 0.05)
    NSGradient(
        starting: NSColor(red: 0.06, green: 0.10, blue: 0.07, alpha: 1),
        ending: NSColor(red: 0.02, green: 0.04, blue: 0.03, alpha: 1)
    )?.draw(in: screen, angle: -90)

    // green phosphor "VT>" with block cursor
    let fontSize = screenRect.height * 0.42
    let font = NSFont(name: "Menlo-Bold", size: fontSize) ?? NSFont.monospacedSystemFont(ofSize: fontSize, weight: .bold)
    let shadow = NSShadow()
    shadow.shadowColor = phosphor.withAlphaComponent(0.8)
    shadow.shadowBlurRadius = s * 0.02
    let attrs: [NSAttributedString.Key: Any] = [.font: font, .foregroundColor: phosphor, .shadow: shadow]
    let text = NSAttributedString(string: "VT", attributes: attrs)
    let ts = text.size()
    let cursorW = ts.width * 0.38
    let totalW = ts.width + cursorW * 1.3
    let tx = screenRect.midX - totalW / 2
    let ty = screenRect.midY - ts.height / 2
    text.draw(at: NSPoint(x: tx, y: ty))
    phosphor.withAlphaComponent(0.9).setFill()
    NSRect(x: tx + ts.width + cursorW * 0.3, y: ty + ts.height * 0.16,
           width: cursorW, height: ts.height * 0.62).fill()

    // scanlines
    if px >= 128 {
        NSColor.black.withAlphaComponent(0.18).setFill()
        var y = screenRect.minY
        while y < screenRect.maxY {
            NSRect(x: screenRect.minX, y: y, width: screenRect.width, height: s * 0.004).fill()
            y += s * 0.016
        }
    }

    img.unlockFocus()

    guard let tiff = img.tiffRepresentation,
          let rep = NSBitmapImageRep(data: tiff),
          let png = rep.representation(using: .png, properties: [:]) else { continue }
    try png.write(to: URL(fileURLWithPath: "\(iconsetPath)/\(name).png"))
}

let task = Process()
task.executableURL = URL(fileURLWithPath: "/usr/bin/iconutil")
task.arguments = ["-c", "icns", "-o", "\(outDir)/AppIcon.icns", iconsetPath]
try task.run()
task.waitUntilExit()
