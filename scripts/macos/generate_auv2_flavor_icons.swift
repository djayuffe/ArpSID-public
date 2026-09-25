#!/usr/bin/env swift

import AppKit
import Foundation

enum IconFlavor: CaseIterable {
    case hybrid
    case instrument
    case drumMachine
    case sid808

    var fileStem: String {
        switch self {
        case .hybrid: return "ArpSIDHybrid"
        case .instrument: return "ArpSIDInstrument"
        case .drumMachine: return "ArpSIDDrumMachine"
        case .sid808: return "ArpSIDSid808"
        }
    }

    var badge: String {
        switch self {
        case .hybrid: return "HYB"
        case .instrument: return "INS"
        case .drumMachine: return "DRM"
        case .sid808: return "808"
        }
    }

    var title: String {
        switch self {
        case .hybrid: return "A"
        case .instrument: return "P"
        case .drumMachine: return "D"
        case .sid808: return "8"
        }
    }

    var backgroundTop: NSColor {
        switch self {
        case .hybrid: return NSColor(calibratedRed: 0.07, green: 0.16, blue: 0.25, alpha: 1.0)
        case .instrument: return NSColor(calibratedRed: 0.22, green: 0.15, blue: 0.05, alpha: 1.0)
        case .drumMachine: return NSColor(calibratedRed: 0.25, green: 0.09, blue: 0.08, alpha: 1.0)
        case .sid808: return NSColor(calibratedRed: 0.20, green: 0.12, blue: 0.04, alpha: 1.0)
        }
    }

    var backgroundBottom: NSColor {
        switch self {
        case .hybrid: return NSColor(calibratedRed: 0.04, green: 0.48, blue: 0.55, alpha: 1.0)
        case .instrument: return NSColor(calibratedRed: 0.86, green: 0.56, blue: 0.16, alpha: 1.0)
        case .drumMachine: return NSColor(calibratedRed: 0.94, green: 0.33, blue: 0.18, alpha: 1.0)
        case .sid808: return NSColor(calibratedRed: 0.97, green: 0.50, blue: 0.13, alpha: 1.0)
        }
    }

    var accent: NSColor {
        switch self {
        case .hybrid: return NSColor(calibratedRed: 0.58, green: 0.99, blue: 0.94, alpha: 1.0)
        case .instrument: return NSColor(calibratedRed: 0.99, green: 0.92, blue: 0.73, alpha: 1.0)
        case .drumMachine: return NSColor(calibratedRed: 1.0, green: 0.82, blue: 0.64, alpha: 1.0)
        case .sid808: return NSColor(calibratedRed: 1.0, green: 0.87, blue: 0.58, alpha: 1.0)
        }
    }
}

enum IconError: Error, CustomStringConvertible {
    case usage
    case renderFailed(String)

    var description: String {
        switch self {
        case .usage:
            return "usage: generate_auv2_flavor_icons.swift <output-dir>"
        case .renderFailed(let message):
            return "icon render failed: \(message)"
        }
    }
}

func ensureDirectory(_ url: URL) throws {
    try FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
}

func removeIfExists(_ url: URL) throws {
    if FileManager.default.fileExists(atPath: url.path) {
        try FileManager.default.removeItem(at: url)
    }
}

func drawScanlines(in rect: NSRect) {
    let path = NSBezierPath()
    let spacing = max(4.0, rect.height * 0.03)
    var y = rect.minY + spacing
    while y < rect.maxY {
        path.move(to: NSPoint(x: rect.minX + rect.width * 0.08, y: y))
        path.line(to: NSPoint(x: rect.maxX - rect.width * 0.08, y: y))
        y += spacing
    }
    path.lineWidth = max(1.0, rect.height * 0.005)
    NSColor.white.withAlphaComponent(0.06).setStroke()
    path.stroke()
}

func drawCommonShell(flavor: IconFlavor, in rect: NSRect) {
    let outer = NSBezierPath(roundedRect: rect, xRadius: rect.width * 0.22, yRadius: rect.height * 0.22)
    NSGradient(starting: flavor.backgroundTop, ending: flavor.backgroundBottom)?.draw(in: outer, angle: -90.0)

    let ringRect = rect.insetBy(dx: rect.width * 0.045, dy: rect.height * 0.045)
    let ring = NSBezierPath(roundedRect: ringRect, xRadius: rect.width * 0.18, yRadius: rect.height * 0.18)
    ring.lineWidth = max(2.0, rect.width * 0.02)
    NSColor.white.withAlphaComponent(0.18).setStroke()
    ring.stroke()

    drawScanlines(in: ringRect)

    let badgeRect = NSRect(
        x: rect.minX + rect.width * 0.10,
        y: rect.maxY - rect.height * 0.27,
        width: rect.width * 0.28,
        height: rect.height * 0.13
    )
    let badge = NSBezierPath(roundedRect: badgeRect, xRadius: badgeRect.height * 0.35, yRadius: badgeRect.height * 0.35)
    NSColor.black.withAlphaComponent(0.20).setFill()
    badge.fill()
    (flavor.badge as NSString).draw(
        in: badgeRect.insetBy(dx: badgeRect.width * 0.12, dy: badgeRect.height * 0.12),
        withAttributes: [
            .font: NSFont.monospacedSystemFont(ofSize: badgeRect.height * 0.52, weight: .bold),
            .foregroundColor: NSColor.white.withAlphaComponent(0.95)
        ]
    )
}

func drawHybridGlyph(in rect: NSRect, accent: NSColor) {
    let chipRect = NSRect(
        x: rect.minX + rect.width * 0.18,
        y: rect.minY + rect.height * 0.18,
        width: rect.width * 0.64,
        height: rect.height * 0.46
    )
    let chip = NSBezierPath(roundedRect: chipRect, xRadius: chipRect.width * 0.08, yRadius: chipRect.height * 0.08)
    NSColor(calibratedWhite: 0.08, alpha: 0.56).setFill()
    chip.fill()
    accent.withAlphaComponent(0.88).setStroke()
    chip.lineWidth = max(2.0, rect.width * 0.02)
    chip.stroke()

    accent.withAlphaComponent(0.72).setFill()
    for idx in 0..<6 {
        let pinW = chipRect.width * 0.06
        let pinH = chipRect.height * 0.08
        let x = chipRect.minX + chipRect.width * 0.08 + CGFloat(idx) * chipRect.width * 0.14
        let yTop = chipRect.maxY + chipRect.height * 0.04
        let yBottom = chipRect.minY - chipRect.height * 0.12
        NSBezierPath(rect: NSRect(x: x, y: yTop, width: pinW, height: pinH)).fill()
        NSBezierPath(rect: NSRect(x: x, y: yBottom, width: pinW, height: pinH)).fill()
    }

    let wave = NSBezierPath()
    let startX = chipRect.minX + chipRect.width * 0.12
    let midY = chipRect.midY
    wave.move(to: NSPoint(x: startX, y: midY))
    wave.line(to: NSPoint(x: startX + chipRect.width * 0.10, y: midY))
    wave.line(to: NSPoint(x: startX + chipRect.width * 0.10, y: midY + chipRect.height * 0.16))
    wave.line(to: NSPoint(x: startX + chipRect.width * 0.28, y: midY + chipRect.height * 0.16))
    wave.line(to: NSPoint(x: startX + chipRect.width * 0.28, y: midY - chipRect.height * 0.10))
    wave.line(to: NSPoint(x: startX + chipRect.width * 0.48, y: midY - chipRect.height * 0.10))
    wave.line(to: NSPoint(x: startX + chipRect.width * 0.48, y: midY + chipRect.height * 0.10))
    wave.line(to: NSPoint(x: startX + chipRect.width * 0.68, y: midY + chipRect.height * 0.10))
    wave.lineWidth = max(3.0, rect.width * 0.024)
    wave.lineCapStyle = .round
    wave.lineJoinStyle = .round
    accent.withAlphaComponent(0.94).setStroke()
    wave.stroke()

    let padRect = NSRect(
        x: chipRect.maxX - chipRect.width * 0.18,
        y: chipRect.minY - chipRect.height * 0.08,
        width: chipRect.width * 0.20,
        height: chipRect.width * 0.20
    )
    let pad = NSBezierPath(ovalIn: padRect)
    NSColor.white.withAlphaComponent(0.16).setFill()
    pad.fill()
    accent.withAlphaComponent(0.95).setStroke()
    pad.lineWidth = max(2.0, rect.width * 0.018)
    pad.stroke()
}

func drawInstrumentGlyph(in rect: NSRect, accent: NSColor) {
    let boardRect = NSRect(
        x: rect.minX + rect.width * 0.14,
        y: rect.minY + rect.height * 0.22,
        width: rect.width * 0.72,
        height: rect.height * 0.22
    )
    let board = NSBezierPath(roundedRect: boardRect, xRadius: boardRect.height * 0.18, yRadius: boardRect.height * 0.18)
    NSColor.black.withAlphaComponent(0.30).setFill()
    board.fill()
    accent.withAlphaComponent(0.85).setStroke()
    board.lineWidth = max(2.0, rect.width * 0.016)
    board.stroke()

    let whiteKeyCount = 6
    let keyWidth = boardRect.width / CGFloat(whiteKeyCount)
    for idx in 0..<whiteKeyCount {
        let keyRect = NSRect(x: boardRect.minX + CGFloat(idx) * keyWidth + 1.0, y: boardRect.minY + 1.0, width: keyWidth - 2.0, height: boardRect.height - 2.0)
        let key = NSBezierPath(roundedRect: keyRect, xRadius: keyRect.width * 0.12, yRadius: keyRect.width * 0.12)
        NSColor.white.withAlphaComponent(0.88).setFill()
        key.fill()
    }

    let blackOffsets: [CGFloat] = [0.70, 1.70, 3.10, 4.10, 5.10]
    for offset in blackOffsets {
        let keyRect = NSRect(
            x: boardRect.minX + keyWidth * offset,
            y: boardRect.midY,
            width: keyWidth * 0.54,
            height: boardRect.height * 0.62
        )
        let key = NSBezierPath(roundedRect: keyRect, xRadius: keyRect.width * 0.18, yRadius: keyRect.width * 0.18)
        accent.withAlphaComponent(0.90).setFill()
        key.fill()
    }

    let waveRect = NSRect(
        x: rect.minX + rect.width * 0.16,
        y: rect.minY + rect.height * 0.52,
        width: rect.width * 0.68,
        height: rect.height * 0.18
    )
    let wave = NSBezierPath()
    wave.move(to: NSPoint(x: waveRect.minX, y: waveRect.midY))
    let segment = waveRect.width / 5.0
    for idx in 0..<5 {
        let x0 = waveRect.minX + CGFloat(idx) * segment
        wave.curve(
            to: NSPoint(x: x0 + segment, y: waveRect.midY),
            controlPoint1: NSPoint(x: x0 + segment * 0.22, y: waveRect.maxY),
            controlPoint2: NSPoint(x: x0 + segment * 0.78, y: waveRect.minY)
        )
    }
    wave.lineWidth = max(4.0, rect.width * 0.024)
    wave.lineCapStyle = .round
    accent.withAlphaComponent(0.96).setStroke()
    wave.stroke()
}

func drawDrumGlyph(in rect: NSRect, accent: NSColor) {
    let gridRect = NSRect(
        x: rect.minX + rect.width * 0.14,
        y: rect.minY + rect.height * 0.24,
        width: rect.width * 0.50,
        height: rect.height * 0.50
    )
    let cellGap = gridRect.width * 0.06
    let cellSize = (gridRect.width - cellGap * 3.0) / 4.0
    for row in 0..<4 {
        for col in 0..<4 {
            let cellRect = NSRect(
                x: gridRect.minX + CGFloat(col) * (cellSize + cellGap),
                y: gridRect.maxY - CGFloat(row + 1) * cellSize - CGFloat(row) * cellGap,
                width: cellSize,
                height: cellSize
            )
            let cell = NSBezierPath(roundedRect: cellRect, xRadius: cellSize * 0.22, yRadius: cellSize * 0.22)
            NSColor.white.withAlphaComponent((row + col) % 3 == 0 ? 0.26 : 0.14).setFill()
            cell.fill()
            accent.withAlphaComponent((row == 0 || col == 0) ? 0.86 : 0.52).setStroke()
            cell.lineWidth = max(1.0, rect.width * 0.010)
            cell.stroke()
        }
    }

    let kickRect = NSRect(
        x: rect.minX + rect.width * 0.60,
        y: rect.minY + rect.height * 0.28,
        width: rect.width * 0.20,
        height: rect.width * 0.20
    )
    let kickOuter = NSBezierPath(ovalIn: kickRect)
    NSColor.black.withAlphaComponent(0.22).setFill()
    kickOuter.fill()
    accent.withAlphaComponent(0.95).setStroke()
    kickOuter.lineWidth = max(3.0, rect.width * 0.020)
    kickOuter.stroke()
    let kickInner = NSBezierPath(ovalIn: kickRect.insetBy(dx: kickRect.width * 0.28, dy: kickRect.height * 0.28))
    accent.withAlphaComponent(0.78).setFill()
    kickInner.fill()

    let stepRect = NSRect(
        x: rect.minX + rect.width * 0.16,
        y: rect.minY + rect.height * 0.15,
        width: rect.width * 0.66,
        height: rect.height * 0.05
    )
    let ledWidth = stepRect.width / 8.0
    for idx in 0..<8 {
        let ledRect = NSRect(
            x: stepRect.minX + CGFloat(idx) * ledWidth + ledWidth * 0.14,
            y: stepRect.minY,
            width: ledWidth * 0.68,
            height: stepRect.height
        )
        let led = NSBezierPath(roundedRect: ledRect, xRadius: ledRect.height * 0.35, yRadius: ledRect.height * 0.35)
        accent.withAlphaComponent(idx % 2 == 0 ? 0.92 : 0.36).setFill()
        led.fill()
    }
}

func drawSid808Glyph(in rect: NSRect, accent: NSColor) {
    drawDrumGlyph(in: rect, accent: accent)

    let railRect = NSRect(
        x: rect.minX + rect.width * 0.16,
        y: rect.minY + rect.height * 0.72,
        width: rect.width * 0.66,
        height: rect.height * 0.032
    )
    let rail = NSBezierPath(roundedRect: railRect, xRadius: railRect.height * 0.40, yRadius: railRect.height * 0.40)
    accent.withAlphaComponent(0.82).setFill()
    rail.fill()

    let textRect = NSRect(
        x: rect.minX + rect.width * 0.58,
        y: rect.minY + rect.height * 0.50,
        width: rect.width * 0.24,
        height: rect.height * 0.14
    )
    ("808" as NSString).draw(
        in: textRect,
        withAttributes: [
            .font: NSFont.monospacedSystemFont(ofSize: textRect.height * 0.72, weight: .black),
            .foregroundColor: accent.withAlphaComponent(0.92)
        ]
    )
}

func drawFlavor(_ flavor: IconFlavor, in rect: NSRect) {
    drawCommonShell(flavor: flavor, in: rect)

    let titleRect = NSRect(
        x: rect.minX + rect.width * 0.62,
        y: rect.maxY - rect.height * 0.33,
        width: rect.width * 0.20,
        height: rect.height * 0.16
    )
    (flavor.title as NSString).draw(
        in: titleRect,
        withAttributes: [
            .font: NSFont.monospacedSystemFont(ofSize: titleRect.height * 0.82, weight: .black),
            .foregroundColor: NSColor.white.withAlphaComponent(0.94)
        ]
    )

    switch flavor {
    case .hybrid:
        drawHybridGlyph(in: rect, accent: flavor.accent)
    case .instrument:
        drawInstrumentGlyph(in: rect, accent: flavor.accent)
    case .drumMachine:
        drawDrumGlyph(in: rect, accent: flavor.accent)
    case .sid808:
        drawSid808Glyph(in: rect, accent: flavor.accent)
    }
}

func renderPNG(flavor: IconFlavor, size: Int, outputURL: URL) throws {
    let canvas = NSSize(width: size, height: size)
    guard let rep = NSBitmapImageRep(
        bitmapDataPlanes: nil,
        pixelsWide: size,
        pixelsHigh: size,
        bitsPerSample: 8,
        samplesPerPixel: 4,
        hasAlpha: true,
        isPlanar: false,
        colorSpaceName: .deviceRGB,
        bytesPerRow: 0,
        bitsPerPixel: 0
    ) else {
        throw IconError.renderFailed("unable to allocate bitmap for \(flavor.fileStem) size \(size)")
    }
    rep.size = canvas
    guard let context = NSGraphicsContext(bitmapImageRep: rep) else {
        throw IconError.renderFailed("unable to create bitmap graphics context for \(flavor.fileStem) size \(size)")
    }

    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = context
    context.imageInterpolation = .high
    NSColor.clear.setFill()
    NSBezierPath(rect: NSRect(origin: .zero, size: canvas)).fill()
    drawFlavor(flavor, in: NSRect(origin: .zero, size: canvas))
    context.flushGraphics()
    NSGraphicsContext.restoreGraphicsState()

    guard let png = rep.representation(using: .png, properties: [:]) else {
        throw IconError.renderFailed("unable to encode PNG for \(flavor.fileStem) size \(size)")
    }
    try png.write(to: outputURL, options: .atomic)
}

func buildIcon(flavor: IconFlavor, outputRoot: URL) throws {
    let pngURL = outputRoot.appendingPathComponent("\(flavor.fileStem).png")
    let legacyIconsetURL = outputRoot.appendingPathComponent("\(flavor.fileStem).iconset", isDirectory: true)
    let legacyIcnsURL = outputRoot.appendingPathComponent("\(flavor.fileStem).icns")
    try removeIfExists(legacyIconsetURL)
    try removeIfExists(legacyIcnsURL)
    try removeIfExists(pngURL)
    try renderPNG(flavor: flavor, size: 1024, outputURL: pngURL)
}

do {
    guard CommandLine.arguments.count == 2 else { throw IconError.usage }
    let outputRoot = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
    try ensureDirectory(outputRoot)
    for flavor in IconFlavor.allCases {
        try buildIcon(flavor: flavor, outputRoot: outputRoot)
    }
} catch {
    fputs("\(error)\n", stderr)
    exit(1)
}
