# -*- coding: utf-8 -*-
"""Generate a short XiaoYunBao user manual PDF."""

from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER, TA_JUSTIFY, TA_LEFT
from reportlab.lib.pagesizes import A5
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (
    PageBreak,
    Paragraph,
    SimpleDocTemplate,
    Spacer,
    Table,
    TableStyle,
)


ROOT = Path(__file__).resolve().parent
OUTPUT = ROOT / "小云宝使用说明手册.pdf"
FONT_DIR = Path(r"C:\Windows\Fonts")

NAVY = colors.HexColor("#1F3A5F")
TEAL = colors.HexColor("#2A6F7F")
INK = colors.HexColor("#243040")
MUTED = colors.HexColor("#5B6673")
LINE = colors.HexColor("#D5DCE3")
BAND = colors.HexColor("#F3F6F8")
WHITE = colors.white
GOLD = colors.HexColor("#C4A35A")


def register_fonts():
    pdfmetrics.registerFont(TTFont("Deng", str(FONT_DIR / "Deng.ttf")))
    pdfmetrics.registerFont(TTFont("DengB", str(FONT_DIR / "Dengb.ttf")))
    pdfmetrics.registerFont(TTFont("SimHei", str(FONT_DIR / "simhei.ttf")))


def make_styles():
    return {
        "kicker": ParagraphStyle(
            "kicker", fontName="DengB", fontSize=10, textColor=GOLD,
            alignment=TA_CENTER, spaceAfter=8,
        ),
        "title": ParagraphStyle(
            "title", fontName="SimHei", fontSize=26, leading=34,
            textColor=NAVY, alignment=TA_CENTER, spaceAfter=8,
        ),
        "sub": ParagraphStyle(
            "sub", fontName="Deng", fontSize=12, leading=18,
            textColor=TEAL, alignment=TA_CENTER, spaceAfter=4,
        ),
        "meta": ParagraphStyle(
            "meta", fontName="Deng", fontSize=10, leading=16,
            textColor=MUTED, alignment=TA_CENTER, spaceAfter=4,
        ),
        "h1": ParagraphStyle(
            "h1", fontName="SimHei", fontSize=14, leading=22,
            textColor=NAVY, spaceBefore=12, spaceAfter=6,
        ),
        "body": ParagraphStyle(
            "body", fontName="Deng", fontSize=10.5, leading=17.5,
            textColor=INK, alignment=TA_JUSTIFY, spaceAfter=6,
        ),
        "step": ParagraphStyle(
            "step", fontName="Deng", fontSize=10.5, leading=17.5,
            textColor=INK, leftIndent=4, spaceAfter=3,
        ),
        "cell": ParagraphStyle(
            "cell", fontName="Deng", fontSize=9, leading=14, textColor=INK,
        ),
        "cell_h": ParagraphStyle(
            "cell_h", fontName="DengB", fontSize=9, leading=14, textColor=WHITE,
        ),
        "note": ParagraphStyle(
            "note", fontName="Deng", fontSize=9, leading=14.5,
            textColor=MUTED, spaceBefore=8, spaceAfter=4,
        ),
        "center_note": ParagraphStyle(
            "center_note", fontName="Deng", fontSize=9.5, leading=15,
            textColor=MUTED, alignment=TA_CENTER, spaceBefore=10,
        ),
    }


def P(style, text):
    return Paragraph(text, style)


def table(headers, rows, widths):
    data = [[P(S["cell_h"], h) for h in headers]]
    for row in rows:
        data.append([P(S["cell"], c) for c in row])
    t = Table(data, colWidths=widths, repeatRows=1)
    t.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, 0), NAVY),
                ("ROWBACKGROUNDS", (0, 1), (-1, -1), [WHITE, BAND]),
                ("VALIGN", (0, 0), (-1, -1), "TOP"),
                ("LEFTPADDING", (0, 0), (-1, -1), 5),
                ("RIGHTPADDING", (0, 0), (-1, -1), 5),
                ("TOPPADDING", (0, 0), (-1, -1), 4),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 4),
                ("GRID", (0, 0), (-1, -1), 0.3, LINE),
            ]
        )
    )
    return t


def header_footer(canvas, doc):
    canvas.saveState()
    w, h = A5
    if doc.page > 1:
        canvas.setFillColor(NAVY)
        canvas.rect(0, h - 10 * mm, w, 10 * mm, fill=1, stroke=0)
        canvas.setFillColor(WHITE)
        canvas.setFont("Deng", 8)
        canvas.drawString(14 * mm, h - 6.4 * mm, "小云宝使用说明")
        canvas.drawRightString(w - 14 * mm, h - 6.4 * mm, "简单手册")
        canvas.setStrokeColor(LINE)
        canvas.setLineWidth(0.4)
        canvas.line(14 * mm, 11 * mm, w - 14 * mm, 11 * mm)
        canvas.setFillColor(MUTED)
        canvas.setFont("Deng", 8)
        canvas.drawString(14 * mm, 6.5 * mm, "2026年9月")
        canvas.drawRightString(w - 14 * mm, 6.5 * mm, "第 %d 页" % doc.page)
    canvas.restoreState()


def build_story():
    s = S
    w = 120 * mm
    story = []

    story.append(Spacer(1, 28 * mm))
    story.append(P(s["kicker"], "CAMPUSCARE"))
    story.append(P(s["title"], "小云宝"))
    story.append(P(s["sub"], "使用说明手册"))
    story.append(Spacer(1, 8 * mm))
    story.append(P(s["meta"], "按一下就能说话的校园陪伴伙伴"))
    story.append(P(s["meta"], "照着做即可"))
    story.append(PageBreak())

    story.append(P(s["h1"], "1. 它是什么"))
    story.append(P(s["body"],
        "小云宝是一台带屏幕和喇叭的小设备。对着它说话，它会回答。"
        "可以中文陪伴，也可以练习英语口语。"))

    story.append(P(s["h1"], "2. 开关机"))
    story.append(P(s["body"],
        "机身侧面有电源开关。"))
    story.append(P(s["step"], "· 开机：把开关拨到「开」，屏幕亮起后即可使用。"))
    story.append(P(s["step"], "· 关机：把开关拨到「关」，设备完全断电。"))
    story.append(P(s["body"],
        "长时间不用请关机，下次使用再打开。"))

    story.append(P(s["h1"], "3. 第一次使用：联网"))
    story.append(P(s["body"], "开机后如果屏幕显示「无网络连接」，按下面步骤连一次 Wi-Fi："))
    story.append(P(s["step"], "1. 确认电源开关已拨到「开」。"))
    story.append(P(s["step"], "2. 用手机连接热点：名称类似 XiaoYunBao-XXXX。"))
    story.append(P(s["step"], "3. 热点密码：xiaoyunbao"))
    story.append(P(s["step"], "4. 手机浏览器打开：192.168.4.1"))
    story.append(P(s["step"], "5. 选择宿舍 Wi-Fi，或填写手机热点名称和密码，点连接。"))
    story.append(P(s["body"], "连成功后会自动记住。以后换网络，长按 K1 即可重新设置。"))

    story.append(P(s["h1"], "4. 四个按键"))
    story.append(table(
        ["键", "短按一下", "按住不放"],
        [
            ["K1", "开始听你说话；听的时候再按表示说完；它正在说话时按一下可打断", "重新设置 Wi-Fi"],
            ["K2", "中文陪伴 和 英语陪练 来回切换", "静音 / 取消静音"],
            ["K3", "屏幕变暗", "音量变小"],
            ["K4", "屏幕变亮", "音量变大"],
        ],
        [16 * mm, 58 * mm, 46 * mm],
    ))
    story.append(Spacer(1, 2 * mm))
    story.append(P(s["body"], "看屏幕上方的小提示，就能确认按键有没有生效。"))

    story.append(P(s["h1"], "5. 中文陪伴"))
    story.append(P(s["step"], "1. 短按 K1，等屏幕出现聆听动画。"))
    story.append(P(s["step"], "2. 对着设备把话说完。"))
    story.append(P(s["step"], "3. 说完后再短按一次 K1，或等它自己判断你说完。"))
    story.append(P(s["step"], "4. 屏幕会显示回答，喇叭同步读出来。"))
    story.append(P(s["body"],
        "靠近一些、环境安静会更清楚。没听清再说一遍即可。"))

    story.append(P(s["h1"], "6. 英语陪练"))
    story.append(P(s["step"], "1. 短按 K2，进入英语模式。"))
    story.append(P(s["step"], "2. 短按 K1 开始说。中文或英语都可以。"))
    story.append(P(s["step"], "3. 它会用英语回答，可能会改你的句子、给一个词，或问下一句。"))
    story.append(P(s["step"], "4. 再按 K2 可回到中文。"))
    story.append(P(s["body"], "英语请说完整一句。"))

    story.append(P(s["h1"], "7. 屏幕常见提示"))
    story.append(table(
        ["屏幕上写着", "意思"],
        [
            ["无网络连接", "还没连上 Wi-Fi，请先配网"],
            ["请唤醒我开始交流", "已联网，短按 K1 即可说话"],
            ["Please speak", "当前是英语陪练"],
            ["配网中", "正在设置网络，请稍等"],
        ],
        [48 * mm, 72 * mm],
    ))
    return story


def main():
    register_fonts()
    global S
    S = make_styles()
    doc = SimpleDocTemplate(
        str(OUTPUT),
        pagesize=A5,
        leftMargin=14 * mm,
        rightMargin=14 * mm,
        topMargin=16 * mm,
        bottomMargin=15 * mm,
        title="小云宝使用说明手册",
        author="小云宝项目组",
    )
    doc.build(build_story(), onFirstPage=header_footer, onLaterPages=header_footer)
    print(OUTPUT)


if __name__ == "__main__":
    main()
