from pathlib import Path
from math import atan2, cos, sin, pi

from reportlab.lib import colors
from reportlab.lib.pagesizes import landscape
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.units import inch
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.pdfgen import canvas
from reportlab.platypus import Paragraph


OUT_DIR = Path(__file__).resolve().parent
PDF_PATH = OUT_DIR / "presentation.pdf"
NOTES_PATH = OUT_DIR / "speaker_notes.md"

PAGE_W = 13.333 * inch
PAGE_H = 7.5 * inch
MARGIN = 0.55 * inch
CONTENT_TOP = PAGE_H - 0.82 * inch
CONTENT_BOTTOM = 0.58 * inch
SLIDE_COUNT = 11


class Palette:
    bg = colors.HexColor("#F4F6F3")
    paper = colors.HexColor("#FFFFFF")
    ink = colors.HexColor("#182027")
    muted = colors.HexColor("#5D6870")
    line = colors.HexColor("#D5DBD2")
    navy = colors.HexColor("#101D26")
    teal = colors.HexColor("#08786F")
    teal_light = colors.HexColor("#E7F3F0")
    blue = colors.HexColor("#315F9D")
    blue_light = colors.HexColor("#EAF0FA")
    green = colors.HexColor("#2F7D4B")
    green_light = colors.HexColor("#EAF4EA")
    amber = colors.HexColor("#A45B12")
    amber_light = colors.HexColor("#FFF2DF")
    coral = colors.HexColor("#C2410C")
    coral_light = colors.HexColor("#FFE7DC")
    violet = colors.HexColor("#5A4AB2")
    violet_light = colors.HexColor("#EFEDFF")
    red = colors.HexColor("#B42318")
    red_light = colors.HexColor("#FCE8E6")
    white = colors.white


def register_fonts():
    fonts_dir = Path("C:/Windows/Fonts")
    try:
        pdfmetrics.registerFont(TTFont("DeckRegular", str(fonts_dir / "arial.ttf")))
        pdfmetrics.registerFont(TTFont("DeckBold", str(fonts_dir / "arialbd.ttf")))
        return "DeckRegular", "DeckBold"
    except Exception:
        return "Helvetica", "Helvetica-Bold"


FONT_REGULAR, FONT_BOLD = register_fonts()


STYLES = {
    "hero": ParagraphStyle(
        "hero",
        fontName=FONT_BOLD,
        fontSize=34,
        leading=40,
        textColor=Palette.white,
    ),
    "hero_sub": ParagraphStyle(
        "hero_sub",
        fontName=FONT_REGULAR,
        fontSize=15,
        leading=20,
        textColor=colors.HexColor("#DDE8EA"),
    ),
    "title": ParagraphStyle(
        "title",
        fontName=FONT_BOLD,
        fontSize=27,
        leading=32,
        textColor=Palette.ink,
    ),
    "subtitle": ParagraphStyle(
        "subtitle",
        fontName=FONT_REGULAR,
        fontSize=14.2,
        leading=19,
        textColor=Palette.muted,
    ),
    "h2": ParagraphStyle(
        "h2",
        fontName=FONT_BOLD,
        fontSize=15,
        leading=18,
        textColor=Palette.ink,
    ),
    "h3": ParagraphStyle(
        "h3",
        fontName=FONT_BOLD,
        fontSize=12.6,
        leading=15,
        textColor=Palette.ink,
    ),
    "body": ParagraphStyle(
        "body",
        fontName=FONT_REGULAR,
        fontSize=10.8,
        leading=14.2,
        textColor=Palette.ink,
    ),
    "small": ParagraphStyle(
        "small",
        fontName=FONT_REGULAR,
        fontSize=9.2,
        leading=12,
        textColor=Palette.muted,
    ),
    "tiny": ParagraphStyle(
        "tiny",
        fontName=FONT_REGULAR,
        fontSize=7.8,
        leading=9.6,
        textColor=Palette.muted,
    ),
    "box": ParagraphStyle(
        "box",
        fontName=FONT_BOLD,
        fontSize=10,
        leading=12,
        alignment=1,
        textColor=Palette.ink,
    ),
    "box_body": ParagraphStyle(
        "box_body",
        fontName=FONT_REGULAR,
        fontSize=8.7,
        leading=10.7,
        alignment=1,
        textColor=Palette.muted,
    ),
}


def scaled_style(base_name, scale=1.0, alignment=None, text_color=None):
    base = STYLES[base_name]
    return ParagraphStyle(
        f"{base_name}_{scale:.2f}_{alignment}",
        parent=base,
        fontSize=base.fontSize * scale,
        leading=base.leading * scale,
        alignment=base.alignment if alignment is None else alignment,
        textColor=base.textColor if text_color is None else text_color,
    )


def para(c, text, x, y, w, h, style="body", alignment=None, color=None, min_scale=0.58):
    scale = 1.0
    p = None
    while scale >= min_scale:
        p = Paragraph(text, scaled_style(style, scale, alignment, color))
        p.wrapOn(c, w, h)
        if p.height <= h:
            break
        scale -= 0.06
    p.drawOn(c, x, y + h - p.height)
    return p.height


def bullet_list(c, items, x, y_top, w, h, style="body", color=None):
    selected = None
    for scale in [1.0, 0.94, 0.88, 0.82, 0.76, 0.70, 0.64, 0.58]:
        item_style = scaled_style(style, scale, text_color=color)
        paras = []
        total = 0
        for item in items:
            p = Paragraph(f"- {item}", item_style)
            p.wrapOn(c, w, h)
            paras.append(p)
            total += p.height + 0.055 * inch
        if total <= h or scale == 0.58:
            selected = paras
            break

    cursor = y_top
    for p in selected:
        p.drawOn(c, x, cursor - p.height)
        cursor -= p.height + 0.055 * inch
    return cursor


def slide_header(c, title, number):
    c.setFillColor(Palette.bg)
    c.rect(0, 0, PAGE_W, PAGE_H, fill=1, stroke=0)
    c.setFillColor(Palette.navy)
    c.rect(0, PAGE_H - 0.5 * inch, PAGE_W, 0.5 * inch, fill=1, stroke=0)
    c.setFillColor(Palette.teal)
    c.rect(0, PAGE_H - 0.5 * inch, 0.12 * inch, 0.5 * inch, fill=1, stroke=0)
    c.setFillColor(Palette.white)
    c.setFont(FONT_BOLD, 12.5)
    c.drawString(MARGIN, PAGE_H - 0.32 * inch, title)
    c.setFont(FONT_REGULAR, 8.8)
    c.drawRightString(PAGE_W - MARGIN, PAGE_H - 0.32 * inch, f"{number}/{SLIDE_COUNT}")


def draw_arrow(c, x1, y1, x2, y2, color=Palette.muted, label=None, dashed=False, width=1.35):
    c.setStrokeColor(color)
    c.setFillColor(color)
    c.setLineWidth(width)
    c.setDash(4, 3) if dashed else c.setDash()
    c.line(x1, y1, x2, y2)
    c.setDash()
    angle = atan2(y2 - y1, x2 - x1)
    size = 7
    left = angle + pi * 0.82
    right = angle - pi * 0.82
    c.line(x2, y2, x2 + size * cos(left), y2 + size * sin(left))
    c.line(x2, y2, x2 + size * cos(right), y2 + size * sin(right))
    if label:
        label_w = max(0.72 * inch, len(label) * 4.5 + 18)
        lx = (x1 + x2) / 2 - label_w / 2
        ly = (y1 + y2) / 2 + 0.07 * inch
        c.setFillColor(Palette.bg)
        c.roundRect(lx, ly - 0.1 * inch, label_w, 0.22 * inch, 5, fill=1, stroke=0)
        c.setFillColor(color)
        c.setFont(FONT_BOLD, 7.8)
        c.drawCentredString((x1 + x2) / 2, ly - 0.01 * inch, label)


def rounded_box(c, x, y, w, h, title, body=None, fill=Palette.paper, stroke=Palette.line, radius=8):
    c.setFillColor(colors.Color(0, 0, 0, alpha=0.05))
    c.roundRect(x + 1.2, y - 1.2, w, h, radius, fill=1, stroke=0)
    c.setFillColor(fill)
    c.setStrokeColor(stroke)
    c.setLineWidth(0.9)
    c.roundRect(x, y, w, h, radius, fill=1, stroke=1)
    c.setFillColor(stroke)
    c.rect(x, y + h - 0.045 * inch, w, 0.045 * inch, fill=1, stroke=0)
    if title:
        title_h = 0.28 * inch if body else min(h - 0.12 * inch, 0.42 * inch)
        para(c, f"<b>{title}</b>", x + 0.10 * inch, y + h - title_h - 0.08 * inch, w - 0.20 * inch, title_h, "box")
    if body:
        para(c, body, x + 0.12 * inch, y + 0.10 * inch, w - 0.24 * inch, h - 0.44 * inch, "box_body")


def card(c, x, y, w, h, heading, items, fill=Palette.paper, stroke=Palette.line):
    c.setFillColor(colors.Color(0, 0, 0, alpha=0.045))
    c.roundRect(x + 1.2, y - 1.2, w, h, 6, fill=1, stroke=0)
    c.setFillColor(Palette.paper)
    c.setStrokeColor(stroke)
    c.setLineWidth(0.9)
    c.roundRect(x, y, w, h, 6, fill=1, stroke=1)
    c.setFillColor(fill)
    c.rect(x, y + h - 0.12 * inch, w, 0.12 * inch, fill=1, stroke=0)
    c.setFillColor(stroke)
    c.rect(x, y, 0.07 * inch, h, fill=1, stroke=0)
    para(c, f"<b>{heading}</b>", x + 0.22 * inch, y + h - 0.46 * inch, w - 0.42 * inch, 0.30 * inch, "h2")
    bullet_list(c, items, x + 0.23 * inch, y + h - 0.62 * inch, w - 0.46 * inch, h - 0.78 * inch, "body")


def label_chip(c, x, y, text, fill, stroke):
    w = max(0.88 * inch, len(text) * 4.4 + 18)
    c.setFillColor(fill)
    c.setStrokeColor(stroke)
    c.roundRect(x, y, w, 0.28 * inch, 8, fill=1, stroke=1)
    c.setFillColor(Palette.ink)
    c.setFont(FONT_BOLD, 8.2)
    c.drawCentredString(x + w / 2, y + 0.095 * inch, text)
    return w


def section_title(c, eyebrow, title, subtitle):
    c.setFillColor(Palette.teal)
    c.setFont(FONT_BOLD, 8.5)
    c.drawString(MARGIN, CONTENT_TOP - 0.12 * inch, eyebrow.upper())
    c.setFillColor(Palette.line)
    c.rect(MARGIN, CONTENT_TOP - 0.24 * inch, 1.15 * inch, 0.012 * inch, fill=1, stroke=0)
    para(c, f"<b>{title}</b>", MARGIN, CONTENT_TOP - 1.02 * inch, 6.25 * inch, 0.68 * inch, "title")
    para(c, subtitle, MARGIN, CONTENT_TOP - 1.58 * inch, 6.85 * inch, 0.48 * inch, "subtitle")


def slide_cover(c):
    c.setFillColor(Palette.navy)
    c.rect(0, 0, PAGE_W, PAGE_H, fill=1, stroke=0)

    c.setFillColor(colors.HexColor("#18313A"))
    c.rect(0, 0, PAGE_W, 1.32 * inch, fill=1, stroke=0)
    c.setFillColor(colors.HexColor("#234E52"))
    c.rect(0, 1.32 * inch, PAGE_W, 0.12 * inch, fill=1, stroke=0)

    para(c, "<b>Understand Any Language<br/>(UAL)</b>", MARGIN, PAGE_H - 2.28 * inch, 6.6 * inch, 1.18 * inch, "hero")
    para(
        c,
        "Understand any language in real life. This two-sensor embedded device senses the environment, captures speech, sends it through a cloud AI pipeline, and plays the result through the speaker.",
        MARGIN,
        PAGE_H - 3.03 * inch,
        7.0 * inch,
        0.62 * inch,
        "hero_sub",
    )

    labels = [
        ("VL53L0X", "distance"),
        ("INMP441", "audio"),
        ("ESP32-S3", "control"),
        ("HTTPS", "internet"),
        ("Cloud AI", "STT + translation + TTS"),
        ("Speaker", "actuator"),
    ]
    x = MARGIN
    y = 1.76 * inch
    for idx, (title, body) in enumerate(labels):
        rounded_box(
            c,
            x,
            y,
            1.72 * inch,
            0.78 * inch,
            title,
            body,
            colors.HexColor("#F7FBFB"),
            colors.HexColor("#5AD8CC") if idx in (0, 1, 4) else colors.HexColor("#AFC0C3"),
        )
        if idx < len(labels) - 1:
            draw_arrow(c, x + 1.72 * inch, y + 0.39 * inch, x + 1.99 * inch, y + 0.39 * inch, colors.HexColor("#9FC5C2"))
        x += 1.99 * inch

    c.setFillColor(colors.HexColor("#D1DFE1"))
    c.setFont(FONT_REGULAR, 11.5)
    c.drawString(MARGIN, 0.74 * inch, "Internet of Things course | End-to-end sensor -> cloud -> actuator prototype")
    c.showPage()


def slide_system_roles(c):
    slide_header(c, "System Components and Roles", 2)
    section_title(
        c,
        "system map",
        "A closed data loop from sensor to actuator",
        "The prototype works as an IoT node that collects environmental data, processes it over the internet, and turns the result into spoken output.",
    )

    rows = [
        ("Sensor 1", "VL53L0X distance sensor: produces proximity and presence data", Palette.teal_light, Palette.teal),
        ("Sensor 2", "INMP441 microphone: samples the speech signal over I2S", Palette.blue_light, Palette.blue),
        ("Microcontroller", "ESP32-S3: manages sensors, packages audio, and runs Wi-Fi communication", Palette.paper, Palette.navy),
        ("Internet layer", "Wi-Fi + TCP/IP + TLS + HTTPS REST connects to the cloud service", Palette.amber_light, Palette.amber),
        ("Cloud AI + actuator", "The OpenRouter STT/translation/TTS chain is sent to the MAX98357A and speaker", Palette.green_light, Palette.green),
    ]
    x_left = MARGIN
    y = 4.54 * inch
    row_h = 0.56 * inch
    row_gap = 0.72 * inch
    for label, answer, fill, stroke in rows:
        rounded_box(c, x_left, y, 2.72 * inch, row_h, label, None, fill, stroke)
        draw_arrow(c, x_left + 2.72 * inch, y + row_h / 2, x_left + 3.15 * inch, y + row_h / 2, stroke)
        rounded_box(c, x_left + 3.22 * inch, y, 8.2 * inch, row_h, answer, None, Palette.paper, stroke)
        y -= row_gap

    rounded_box(
        c,
        MARGIN,
        0.50 * inch,
        PAGE_W - 2 * MARGIN,
        0.62 * inch,
        "Data loop",
        "Distance and audio data from the physical environment are controlled on the ESP32-S3, processed in the cloud AI chain, and returned to the physical world through the speaker.",
        Palette.paper,
        Palette.line,
    )
    c.showPage()


def slide_pipeline(c):
    slide_header(c, "End-to-End IoT Pipeline", 3)
    section_title(
        c,
        "main architecture",
        "Data moves from the physical world to the cloud and back",
        "Each block is a link in the chain: sensing, control, internet transport, cloud AI processing, and physical output.",
    )

    y = 3.75 * inch
    stages = [
        ("Sensors", "Distance + audio data", Palette.teal_light, Palette.teal),
        ("ESP32-S3", "Pre-control + packaging", Palette.blue_light, Palette.blue),
        ("HTTPS", "Cloud over TCP/IP", Palette.amber_light, Palette.amber),
        ("Cloud AI", "STT -> LLM -> TTS", Palette.violet_light, Palette.violet),
        ("Speaker", "Audio output", Palette.green_light, Palette.green),
    ]
    x = MARGIN
    w = 2.02 * inch
    gap = 0.39 * inch
    for idx, (title, body, fill, stroke) in enumerate(stages):
        rounded_box(c, x, y, w, 1.02 * inch, title, body, fill, stroke)
        if idx < len(stages) - 1:
            draw_arrow(c, x + w, y + 0.51 * inch, x + w + gap - 0.08 * inch, y + 0.51 * inch, stroke)
        x += w + gap

    rounded_box(c, MARGIN, 2.15 * inch, 2.35 * inch, 0.72 * inch, "VL53L0X", "proximity / presence", Palette.teal_light, Palette.teal)
    rounded_box(c, MARGIN, 1.24 * inch, 2.35 * inch, 0.72 * inch, "INMP441", "speech / audio", Palette.blue_light, Palette.blue)
    draw_arrow(c, MARGIN + 2.35 * inch, 2.51 * inch, MARGIN + 3.06 * inch, 4.09 * inch, Palette.teal, "I2C")
    draw_arrow(c, MARGIN + 2.35 * inch, 1.60 * inch, MARGIN + 3.06 * inch, 4.00 * inch, Palette.blue, "I2S")

    card(
        c,
        MARGIN + 3.15 * inch,
        1.05 * inch,
        3.0 * inch,
        1.82 * inch,
        "ESP32-S3 role",
        [
            "Reads sensors and starts the recording session",
            "Captures audio as 16 kHz PCM",
            "Builds HTTP bodies and streams the cloud response",
        ],
        Palette.paper,
        Palette.line,
    )
    card(
        c,
        MARGIN + 6.47 * inch,
        1.05 * inch,
        3.0 * inch,
        1.82 * inch,
        "Cloud role",
        [
            "Converts speech to text",
            "Translates text into the target language",
            "Converts the translation back to speech",
        ],
        Palette.paper,
        Palette.line,
    )
    card(
        c,
        MARGIN + 9.78 * inch,
        1.05 * inch,
        1.95 * inch,
        1.82 * inch,
        "Physical output",
        [
            "TTS PCM data goes to the amplifier over I2S",
            "The speaker produces physical output",
        ],
        Palette.green_light,
        Palette.green,
    )
    c.showPage()


def slide_sensor_rules(c):
    slide_header(c, "Sensor and Data Capture Rules", 4)
    section_title(
        c,
        "sensor layer",
        "The device senses the environment through distance and audio",
        "The distance sensor starts the recording session; the microphone moves speech data into ESP32-S3 memory in regular chunks.",
    )

    card(
        c,
        MARGIN,
        2.35 * inch,
        3.65 * inch,
        2.1 * inch,
        "VL53L0X distance sensor",
        [
            "Read over I2C every 150 ms",
            "Presence triggers at 1000 mm or less",
            "Presence clears above 1200 mm",
            "3.5 s retrigger delay",
        ],
        Palette.teal_light,
        Palette.teal,
    )
    card(
        c,
        MARGIN + 4.0 * inch,
        2.35 * inch,
        3.65 * inch,
        2.1 * inch,
        "INMP441 microphone sensor",
        [
            "Reads 32-bit samples over I2S RX",
            "Samples are downconverted to 16-bit PCM",
            "Processed in 320-sample chunks",
            "RMS and peak thresholds determine speech presence",
        ],
        Palette.blue_light,
        Palette.blue,
    )
    card(
        c,
        MARGIN + 8.0 * inch,
        2.35 * inch,
        3.65 * inch,
        2.1 * inch,
        "Recording session rules",
        [
            "Starts by automatic trigger or web/serial command",
            "Recording stops after 3 seconds of silence",
            "30-second maximum recording window",
            "Cloud processing does not start if no speech is detected",
        ],
        Palette.amber_light,
        Palette.amber,
    )

    rounded_box(c, MARGIN, 1.12 * inch, 2.15 * inch, 0.72 * inch, "Distance", "VL53L0X measurement", Palette.teal_light, Palette.teal)
    rounded_box(c, MARGIN + 2.72 * inch, 1.12 * inch, 2.15 * inch, 0.72 * inch, "Trigger", "<= 1000 mm", Palette.paper, Palette.teal)
    rounded_box(c, MARGIN + 5.44 * inch, 1.12 * inch, 2.15 * inch, 0.72 * inch, "Recording", "INMP441 PCM", Palette.blue_light, Palette.blue)
    rounded_box(c, MARGIN + 8.16 * inch, 1.12 * inch, 2.15 * inch, 0.72 * inch, "Stop", "3 s silence", Palette.paper, Palette.amber)
    draw_arrow(c, MARGIN + 2.15 * inch, 1.48 * inch, MARGIN + 2.72 * inch, 1.48 * inch, Palette.teal)
    draw_arrow(c, MARGIN + 4.87 * inch, 1.48 * inch, MARGIN + 5.44 * inch, 1.48 * inch, Palette.blue)
    draw_arrow(c, MARGIN + 7.59 * inch, 1.48 * inch, MARGIN + 8.16 * inch, 1.48 * inch, Palette.amber)
    c.showPage()


def slide_audio_format(c):
    slide_header(c, "Audio Format and Chunking", 5)
    section_title(
        c,
        "audio data",
        "The microphone does not create a file; ESP32-S3 turns the sample stream into a PCM recording",
        "Speech first arrives as raw I2S samples, then is written into a 16 kHz mono PCM buffer and converted into a WAV/Base64 body for the cloud.",
    )

    steps = [
        ("I2S read", "INMP441<br/>32-bit frame", Palette.blue_light, Palette.blue),
        ("PCM conversion", "24-bit meaningful data<br/>16-bit signed PCM", Palette.teal_light, Palette.teal),
        ("Chunking", "320 samples<br/>about 20 ms", Palette.amber_light, Palette.amber),
        ("Recording buffer", "16 kHz mono<br/>max. 30 s", Palette.paper, Palette.navy),
        ("STT package", "44-byte WAV header<br/>Base64 JSON", Palette.violet_light, Palette.violet),
    ]
    x = MARGIN
    y = 3.72 * inch
    w = 2.05 * inch
    for idx, (title, body, fill, stroke) in enumerate(steps):
        rounded_box(c, x, y, w, 1.02 * inch, title, body, fill, stroke)
        if idx < len(steps) - 1:
            draw_arrow(c, x + w, y + 0.51 * inch, x + w + 0.32 * inch, y + 0.51 * inch, stroke)
        x += 2.42 * inch

    card(
        c,
        MARGIN,
        1.05 * inch,
        3.62 * inch,
        1.78 * inch,
        "Sampling format",
        [
            "Sample rate: 16 kHz",
            "Channel: mono",
            "Sample type: signed PCM16",
            "30 s recording produces about 960 KB raw PCM",
        ],
        Palette.paper,
        Palette.line,
    )
    card(
        c,
        MARGIN + 3.96 * inch,
        1.05 * inch,
        3.62 * inch,
        1.78 * inch,
        "Speech detection",
        [
            "Chunk average is measured and DC offset is removed",
            "RMS threshold: 900",
            "Peak threshold: 2500",
            "Speech is accepted when both thresholds pass",
        ],
        Palette.paper,
        Palette.line,
    )
    card(
        c,
        MARGIN + 7.92 * inch,
        1.05 * inch,
        3.62 * inch,
        1.78 * inch,
        "Cloud preparation",
        [
            "Raw PCM is not sent directly to STT",
            "A WAV header is prepended",
            "WAV data is carried as base64 inside JSON",
            "The large body is written in chunks",
        ],
        Palette.paper,
        Palette.line,
    )
    c.showPage()


def slide_protocol(c):
    slide_header(c, "Internet Protocols and API Communication", 6)
    section_title(
        c,
        "network layer",
        "ESP32-S3 sends data to OpenRouter with HTTPS REST requests",
        "The network flow starts with Wi-Fi, passes through DNS, TCP/IP, and TLS, then becomes HTTP POST requests.",
    )

    stack = [
        ("Wi-Fi", "connects to local network", Palette.teal_light, Palette.teal),
        ("TCP/IP", "end-to-end packet transport", Palette.blue_light, Palette.blue),
        ("DNS", "resolves openrouter.ai", Palette.amber_light, Palette.amber),
        ("TLS", "encrypted connection", Palette.violet_light, Palette.violet),
        ("HTTPS REST", "OpenRouter POST", Palette.green_light, Palette.green),
    ]
    x = MARGIN
    y = 3.92 * inch
    for idx, (title, body, fill, stroke) in enumerate(stack):
        rounded_box(c, x, y, 2.05 * inch, 0.92 * inch, title, body, fill, stroke)
        if idx < len(stack) - 1:
            draw_arrow(c, x + 2.05 * inch, y + 0.46 * inch, x + 2.38 * inch, y + 0.46 * inch, stroke)
        x += 2.40 * inch

    card(
        c,
        MARGIN,
        1.10 * inch,
        3.62 * inch,
        1.96 * inch,
        "STT request",
        [
            "Endpoint: /api/v1/audio/transcriptions",
            "Body: model + input_audio JSON",
            "Audio: base64 data with WAV header",
            "Response: transcript text",
        ],
        Palette.paper,
        Palette.line,
    )
    card(
        c,
        MARGIN + 3.96 * inch,
        1.10 * inch,
        3.62 * inch,
        1.96 * inch,
        "Translation request",
        [
            "Endpoint: /api/v1/chat/completions",
            "Body: system + user messages",
            "Temperature 0 for steadier translation",
            "Response: target-language text",
        ],
        Palette.paper,
        Palette.line,
    )
    card(
        c,
        MARGIN + 7.92 * inch,
        1.10 * inch,
        3.62 * inch,
        1.96 * inch,
        "TTS request",
        [
            "Endpoint: /api/v1/audio/speech",
            "Body: input, voice, speed, response_format",
            "response_format: pcm",
            "Response: Content-Length or chunked PCM stream",
        ],
        Palette.paper,
        Palette.line,
    )
    c.showPage()


def slide_cloud_ai(c):
    slide_header(c, "Cloud AI Models", 7)
    section_title(
        c,
        "cloud layer",
        "Three separate AI tasks run in sequence on OpenRouter",
        "ESP32-S3 does not run the models; it sends audio and text to the cloud, reads cloud responses, and passes them to the next step.",
    )

    card(
        c,
        MARGIN,
        2.25 * inch,
        3.62 * inch,
        2.18 * inch,
        "1. STT - Speech to Text",
        [
            "Model: openai/whisper-large-v3",
            "Task: convert speech to text",
            "The Whisper family uses transformer-based speech recognition",
            "Output: transcript text",
        ],
        Palette.blue_light,
        Palette.blue,
    )
    card(
        c,
        MARGIN + 3.96 * inch,
        2.25 * inch,
        3.62 * inch,
        2.18 * inch,
        "2. LLM translation",
        [
            "Model: mistralai/mistral-small-2603",
            "Task: translate the transcript into the target language",
            "Generates text with decoder-only LLM behavior",
            "Output: translated text only",
        ],
        Palette.violet_light,
        Palette.violet,
    )
    card(
        c,
        MARGIN + 7.92 * inch,
        2.25 * inch,
        3.62 * inch,
        2.18 * inch,
        "3. TTS - Text to Speech",
        [
            "Model: openai/gpt-4o-mini-tts-2025-12-15",
            "Task: turn translated text into speech",
            "Neural TTS: produces a speech waveform from text",
            "Voice: alloy, speed: 1.0",
            "Output: raw PCM audio stream",
        ],
        Palette.green_light,
        Palette.green,
    )

    rounded_box(c, MARGIN, 1.02 * inch, 2.4 * inch, 0.7 * inch, "STT", "audio -> text", Palette.blue_light, Palette.blue)
    rounded_box(c, MARGIN + 2.95 * inch, 1.02 * inch, 2.4 * inch, 0.7 * inch, "LLM", "text -> text", Palette.violet_light, Palette.violet)
    rounded_box(c, MARGIN + 5.9 * inch, 1.02 * inch, 2.4 * inch, 0.7 * inch, "TTS", "text -> audio", Palette.green_light, Palette.green)
    rounded_box(c, MARGIN + 8.85 * inch, 1.02 * inch, 2.4 * inch, 0.7 * inch, "ESP32-S3", "streams the result", Palette.paper, Palette.navy)
    draw_arrow(c, MARGIN + 2.4 * inch, 1.37 * inch, MARGIN + 2.95 * inch, 1.37 * inch, Palette.violet)
    draw_arrow(c, MARGIN + 5.35 * inch, 1.37 * inch, MARGIN + 5.9 * inch, 1.37 * inch, Palette.green)
    draw_arrow(c, MARGIN + 8.3 * inch, 1.37 * inch, MARGIN + 8.85 * inch, 1.37 * inch, Palette.navy)
    c.showPage()


def slide_speaker_output(c):
    slide_header(c, "Speaker Data Transfer", 8)
    section_title(
        c,
        "actuator layer",
        "The TTS response is not downloaded as a file; the PCM stream is played in chunks",
        "Firmware aligns raw PCM bytes from the HTTP body, converts them to I2S output, and sends them through the MAX98357A amplifier to the speaker.",
    )

    steps = [
        ("HTTP body", "audio/pcm<br/>or octet-stream", Palette.violet_light, Palette.violet),
        ("Network buffer", "512 bytes<br/>chunked reading", Palette.amber_light, Palette.amber),
        ("PCM alignment", "16-bit sample<br/>single byte preserved", Palette.blue_light, Palette.blue),
        ("I2S frame", "32-bit left/right<br/>same sample", Palette.teal_light, Palette.teal),
        ("MAX98357A", "amp + speaker<br/>audio output", Palette.green_light, Palette.green),
    ]
    x = MARGIN
    y = 3.72 * inch
    w = 2.05 * inch
    for idx, (title, body, fill, stroke) in enumerate(steps):
        rounded_box(c, x, y, w, 1.02 * inch, title, body, fill, stroke)
        if idx < len(steps) - 1:
            draw_arrow(c, x + w, y + 0.51 * inch, x + w + 0.32 * inch, y + 0.51 * inch, stroke)
        x += 2.42 * inch

    card(
        c,
        MARGIN,
        1.05 * inch,
        3.62 * inch,
        1.78 * inch,
        "Stream reading",
        [
            "If the response is chunked, each HTTP chunk is read separately",
            "If Content-Length exists, remaining byte count is tracked",
            "Large audio is not fully loaded into RAM",
        ],
        Palette.paper,
        Palette.line,
    )
    card(
        c,
        MARGIN + 3.96 * inch,
        1.05 * inch,
        3.62 * inch,
        1.78 * inch,
        "PCM playback",
        [
            "PCM16 samples are kept 2-byte aligned",
            "Playback is multiplied by gain",
            "Overflow is clipped to int16 range",
            "Sample is expanded to a 32-bit I2S frame",
        ],
        Palette.paper,
        Palette.line,
    )
    card(
        c,
        MARGIN + 7.92 * inch,
        1.05 * inch,
        3.62 * inch,
        1.78 * inch,
        "Speaker setup",
        [
            "Default TTS sample rate is 24 kHz",
            "The I2S driver restarts if needed",
            "Mono audio is copied to left and right channels",
            "MAX98357A converts the digital I2S signal to analog sound",
        ],
        Palette.paper,
        Palette.line,
    )
    c.showPage()


def slide_device_logic(c):
    slide_header(c, "ESP32-S3 Control Logic", 9)
    section_title(
        c,
        "device behavior",
        "Firmware behaves like a state machine",
        "The device listens to the web panel, tracks sensors, manages recording state, and returns to ready state after cloud processing.",
    )

    states = [
        ("Ready", "Wi-Fi and panel active", Palette.paper, Palette.line),
        ("Detect", "VL53L0X or web button", Palette.teal_light, Palette.teal),
        ("Listen", "INMP441 PCM recording", Palette.blue_light, Palette.blue),
        ("Send to cloud", "HTTPS AI pipeline", Palette.violet_light, Palette.violet),
        ("Speak", "PCM to speaker", Palette.green_light, Palette.green),
    ]
    x = MARGIN
    y = 3.98 * inch
    w = 1.88 * inch
    for idx, (title, body, fill, stroke) in enumerate(states):
        rounded_box(c, x, y, w, 0.96 * inch, title, body, fill, stroke)
        if idx < len(states) - 1:
            draw_arrow(c, x + w, y + 0.48 * inch, x + w + 0.36 * inch, y + 0.48 * inch, stroke)
        x += w + 0.42 * inch

    card(
        c,
        MARGIN,
        1.16 * inch,
        3.62 * inch,
        1.9 * inch,
        "Automatic trigger",
        [
            "Distance sensor is read at regular intervals",
            "A new recording starts when the user moves below the threshold",
            "Manual recording is also possible from the web panel",
        ],
        Palette.paper,
        Palette.line,
    )
    card(
        c,
        MARGIN + 3.96 * inch,
        1.16 * inch,
        3.62 * inch,
        1.9 * inch,
        "Recording management",
        [
            "Speech is stored as 16 kHz mono PCM",
            "Recording ends when silence is detected",
            "The maximum recording window prevents memory overflow",
        ],
        Palette.paper,
        Palette.line,
    )
    card(
        c,
        MARGIN + 7.92 * inch,
        1.16 * inch,
        3.62 * inch,
        1.9 * inch,
        "Feedback",
        [
            "Status is shown on the web panel",
            "Last text and translation are visible in the panel",
            "Speaker output is the system's physical response",
        ],
        Palette.paper,
        Palette.line,
    )
    c.showPage()


def slide_web_panel(c):
    slide_header(c, "Web Panel and System Monitoring", 10)
    section_title(
        c,
        "human interface",
        "The web panel makes the device's IoT behavior visible",
        "The browser interface shows connection status, recording phase, transcript, and translation result live.",
    )

    rounded_box(c, MARGIN, 3.86 * inch, 3.12 * inch, 1.05 * inch, "Connection status", "Wi-Fi IP, RSSI, DNS, and operating mode", Palette.blue_light, Palette.blue)
    rounded_box(c, MARGIN, 2.35 * inch, 3.12 * inch, 1.05 * inch, "Voice recording", "Start recording, listening, and speech states", Palette.teal_light, Palette.teal)
    rounded_box(c, MARGIN + 4.32 * inch, 3.07 * inch, 3.0 * inch, 1.2 * inch, "ESP32 WebServer", "Port 80<br/>JSON-based control API", Palette.paper, Palette.navy)
    rounded_box(c, MARGIN + 8.55 * inch, 3.86 * inch, 3.1 * inch, 1.05 * inch, "Last transcript", "Speech detected by STT", Palette.violet_light, Palette.violet)
    rounded_box(c, MARGIN + 8.55 * inch, 2.35 * inch, 3.1 * inch, 1.05 * inch, "Last translation", "Target language sent to speaker by TTS", Palette.green_light, Palette.green)

    draw_arrow(c, MARGIN + 3.12 * inch, 4.38 * inch, MARGIN + 4.32 * inch, 3.85 * inch, Palette.blue)
    draw_arrow(c, MARGIN + 3.12 * inch, 2.88 * inch, MARGIN + 4.32 * inch, 3.45 * inch, Palette.teal)
    draw_arrow(c, MARGIN + 7.32 * inch, 3.85 * inch, MARGIN + 8.55 * inch, 4.38 * inch, Palette.violet)
    draw_arrow(c, MARGIN + 7.32 * inch, 3.45 * inch, MARGIN + 8.55 * inch, 2.88 * inch, Palette.green)

    card(
        c,
        MARGIN,
        0.92 * inch,
        3.62 * inch,
        1.02 * inch,
        "During use",
        [
            "First, IP and sensor readiness are shown",
            "Then recording starts and the pipeline is tracked",
        ],
        Palette.paper,
        Palette.line,
    )
    card(
        c,
        MARGIN + 3.96 * inch,
        0.92 * inch,
        3.62 * inch,
        1.02 * inch,
        "Observability",
        [
            "The system does not look like a black box",
            "Data, status, and output are monitored on one screen",
        ],
        Palette.paper,
        Palette.line,
    )
    card(
        c,
        MARGIN + 7.92 * inch,
        0.92 * inch,
        3.62 * inch,
        1.02 * inch,
        "Settings management",
        [
            "Wi-Fi, language, model, and audio settings are saved",
            "The status API periodically updates the web panel",
        ],
        Palette.paper,
        Palette.line,
    )
    c.showPage()


def slide_conclusion(c):
    slide_header(c, "Conclusion and Improvement Ideas", 11)
    section_title(
        c,
        "closing",
        "The project is completed as an IoT pipeline",
        "The focus is no longer the number of parts, but the closed loop between physical data, network, cloud AI, and physical output.",
    )

    card(
        c,
        MARGIN,
        2.35 * inch,
        3.62 * inch,
        2.15 * inch,
        "Strengths",
        [
            "Sensor, network, cloud AI, and actuator layers are clearly separated",
            "The cloud AI chain is live and observable",
            "The web panel makes diagnostics and monitoring easier",
        ],
        Palette.green_light,
        Palette.green,
    )
    card(
        c,
        MARGIN + 3.96 * inch,
        2.35 * inch,
        3.62 * inch,
        2.15 * inch,
        "Limitations",
        [
            "Requires an internet connection and OpenRouter access",
            "Ambient noise can affect STT success",
            "API key and network access must be controlled",
        ],
        Palette.red_light,
        Palette.red,
    )
    card(
        c,
        MARGIN + 7.92 * inch,
        2.35 * inch,
        3.62 * inch,
        2.15 * inch,
        "Improvements",
        [
            "Authenticated web panel on the device side",
            "Move TLS certificate verification to production level",
            "Noise reduction and better microphone calibration",
        ],
        Palette.blue_light,
        Palette.blue,
    )

    rounded_box(
        c,
        MARGIN,
        0.78 * inch,
        PAGE_W - 2 * MARGIN,
        0.86 * inch,
        "Closing message",
        "ESP32-S3 works as an end-to-end IoT prototype that sends real-world data from two sensors to cloud AI over HTTPS and plays the result through a speaker actuator.",
        Palette.paper,
        Palette.line,
    )
    c.showPage()


NOTES = """# Understand Any Language (UAL) - Speaker Notes

These notes are not placed in the PDF; they are for presentation support and question prep.

## Main Message

The project is an IoT data chain:

```text
VL53L0X distance sensor + INMP441 microphone sensor
        |
ESP32-S3 microcontroller
        |
Wi-Fi / TCP-IP / TLS / HTTPS REST
        |
OpenRouter cloud AI chain
STT -> translation LLM -> TTS
        |
MAX98357A amplifier + speaker actuator
```

## Slide 1 - Cover

One sentence: The device collects data from two sensors, sends it to the internet with ESP32-S3, processes it in cloud AI, and plays the result through the speaker.

## Slide 2 - System Components

Clearly separate the roles of sensor, microcontroller, internet layer, cloud AI, and actuator.

## Slide 3 - Pipeline

Flow: sensors -> ESP32-S3 -> HTTPS -> cloud AI -> speaker.

## Slide 4 - Sensor Rules

- VL53L0X is read every 150 ms.
- Presence triggers at 1000 mm or less.
- Presence clears above 1200 mm.
- There is a 3.5-second retrigger delay.
- INMP441 sends the speech signal over I2S.

## Slide 5 - Audio Format

- The microphone does not create a file; ESP32 reads the sample stream.
- Recording is stored as 16 kHz mono signed PCM16.
- 320 samples are about a 20 ms audio chunk.
- A 44-byte WAV header is prepended for STT.
- WAV data is written into the JSON body as base64.

## Slide 6 - Internet Protocols

Network chain: Wi-Fi -> DNS -> TCP/IP -> TLS -> HTTPS REST.

OpenRouter endpoints:

- `/api/v1/audio/transcriptions`: STT.
- `/api/v1/chat/completions`: translation.
- `/api/v1/audio/speech`: TTS.

## Slide 7 - Cloud AI Models

- STT: `openai/whisper-large-v3`, converts speech to text.
- Translation: `mistralai/mistral-small-2603`, translates the transcript into the target language.
- TTS: `openai/gpt-4o-mini-tts-2025-12-15`, turns text into raw PCM speech with neural TTS.

STT = Speech to Text. TTS = Text to Speech.

## Slide 8 - Speaker Transfer

The TTS response is not downloaded as a file. Firmware reads PCM bytes from the HTTP body through a 512-byte network buffer, preserves PCM16 alignment, converts samples into 32-bit I2S frames, and sends them to the MAX98357A amplifier.

## Slide 9 - Control Logic

State flow:

```text
Ready -> Detect -> Listen -> Send to cloud -> Speak -> Ready
```

## Slide 10 - Web Panel

The panel shows connection, sensor, recording, transcript, and translation states. Wi-Fi, language, model, and audio settings can also be managed there.

## Slide 11 - Closing

Closing sentence:

"ESP32-S3 works as an end-to-end IoT prototype that sends real-world data from two sensors to cloud AI over HTTPS and plays the result through a speaker actuator."

## Possible Short Answers

### Does it work without internet?

No. STT, translation, and TTS run in the cloud, so internet access is required.

### Why cloud AI?

ESP32-S3 is not suitable for running large STT/LLM/TTS models locally. Cloud processing gives better model quality and updatability.

### Why raw PCM?

Raw PCM can be streamed directly over I2S to the speaker. MP3 would require an additional decoder.

### What is the weakest point?

The system depends on internet access and API quota. Productization would require web panel authentication, production-grade TLS verification, and stronger privacy measures.
"""


def build_pdf():
    c = canvas.Canvas(str(PDF_PATH), pagesize=landscape((PAGE_W, PAGE_H)))
    c.setTitle("Understand Any Language (UAL)")
    c.setAuthor("Codex")
    slide_cover(c)
    slide_system_roles(c)
    slide_pipeline(c)
    slide_sensor_rules(c)
    slide_audio_format(c)
    slide_protocol(c)
    slide_cloud_ai(c)
    slide_speaker_output(c)
    slide_device_logic(c)
    slide_web_panel(c)
    slide_conclusion(c)
    c.save()
    NOTES_PATH.write_text(NOTES, encoding="utf-8")


if __name__ == "__main__":
    build_pdf()
    print(PDF_PATH)
    print(NOTES_PATH)
