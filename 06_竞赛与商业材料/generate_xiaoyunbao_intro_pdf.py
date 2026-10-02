# -*- coding: utf-8 -*-
"""Generate the XiaoYunBao / CampusCare project briefing PDF."""

from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER, TA_JUSTIFY, TA_LEFT, TA_RIGHT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (
    KeepTogether,
    ListFlowable,
    ListItem,
    PageBreak,
    Paragraph,
    SimpleDocTemplate,
    Spacer,
    Table,
    TableStyle,
)


ROOT = Path(__file__).resolve().parent
OUTPUT = ROOT / "小云宝项目全方位讲解-2026-09-20.pdf"
FONT_DIR = Path(r"C:\Windows\Fonts")

NAVY = colors.HexColor("#1F3A5F")
TEAL = colors.HexColor("#2A6F7F")
GOLD = colors.HexColor("#C4A35A")
INK = colors.HexColor("#243040")
MUTED = colors.HexColor("#5B6673")
LINE = colors.HexColor("#D5DCE3")
BAND = colors.HexColor("#F3F6F8")
WHITE = colors.white


def register_fonts():
    pdfmetrics.registerFont(TTFont("Deng", str(FONT_DIR / "Deng.ttf")))
    pdfmetrics.registerFont(TTFont("DengB", str(FONT_DIR / "Dengb.ttf")))
    pdfmetrics.registerFont(TTFont("SimHei", str(FONT_DIR / "simhei.ttf")))


def styles():
    base = getSampleStyleSheet()
    s = {}
    s["cover_kicker"] = ParagraphStyle(
        "cover_kicker",
        fontName="DengB",
        fontSize=11,
        textColor=GOLD,
        alignment=TA_CENTER,
        letterSpacing=1.2,
        spaceAfter=10,
    )
    s["cover_title"] = ParagraphStyle(
        "cover_title",
        fontName="SimHei",
        fontSize=32,
        leading=40,
        textColor=NAVY,
        alignment=TA_CENTER,
        spaceAfter=8,
    )
    s["cover_sub"] = ParagraphStyle(
        "cover_sub",
        fontName="DengB",
        fontSize=14,
        leading=22,
        textColor=TEAL,
        alignment=TA_CENTER,
        spaceAfter=6,
    )
    s["cover_meta"] = ParagraphStyle(
        "cover_meta",
        fontName="Deng",
        fontSize=11,
        leading=18,
        textColor=MUTED,
        alignment=TA_CENTER,
        spaceAfter=4,
    )
    s["h1"] = ParagraphStyle(
        "h1",
        fontName="SimHei",
        fontSize=16,
        leading=24,
        textColor=NAVY,
        spaceBefore=16,
        spaceAfter=8,
        borderPadding=0,
    )
    s["h2"] = ParagraphStyle(
        "h2",
        fontName="DengB",
        fontSize=12.5,
        leading=20,
        textColor=TEAL,
        spaceBefore=10,
        spaceAfter=5,
    )
    s["body"] = ParagraphStyle(
        "body",
        fontName="Deng",
        fontSize=10.5,
        leading=18,
        textColor=INK,
        alignment=TA_JUSTIFY,
        firstLineIndent=0,
        spaceAfter=7,
    )
    s["lead"] = ParagraphStyle(
        "lead",
        fontName="Deng",
        fontSize=11.5,
        leading=20,
        textColor=NAVY,
        alignment=TA_JUSTIFY,
        spaceAfter=10,
    )
    s["bullet"] = ParagraphStyle(
        "bullet",
        fontName="Deng",
        fontSize=10.5,
        leading=17.5,
        textColor=INK,
        leftIndent=12,
        spaceAfter=3,
    )
    s["cell"] = ParagraphStyle(
        "cell",
        fontName="Deng",
        fontSize=9,
        leading=14,
        textColor=INK,
    )
    s["cell_h"] = ParagraphStyle(
        "cell_h",
        fontName="DengB",
        fontSize=9,
        leading=14,
        textColor=WHITE,
    )
    s["caption"] = ParagraphStyle(
        "caption",
        fontName="Deng",
        fontSize=8.5,
        leading=13,
        textColor=MUTED,
        alignment=TA_CENTER,
        spaceBefore=2,
        spaceAfter=10,
    )
    s["quote"] = ParagraphStyle(
        "quote",
        fontName="Deng",
        fontSize=10.5,
        leading=18,
        textColor=NAVY,
        leftIndent=10,
        rightIndent=10,
        spaceBefore=4,
        spaceAfter=10,
        borderColor=GOLD,
        borderPadding=8,
    )
    s["toc"] = ParagraphStyle(
        "toc",
        fontName="Deng",
        fontSize=11,
        leading=20,
        textColor=INK,
        leftIndent=6,
        spaceAfter=2,
    )
    s["footer"] = ParagraphStyle(
        "footer",
        fontName="Deng",
        fontSize=8,
        textColor=MUTED,
        alignment=TA_CENTER,
    )
    s["note"] = ParagraphStyle(
        "note",
        fontName="Deng",
        fontSize=9.5,
        leading=15,
        textColor=MUTED,
        spaceAfter=8,
    )
    return s


def p(style, text):
    return Paragraph(text.replace("\n", "<br/>"), style)


def bullets(style, items):
    flow = []
    for item in items:
        flow.append(p(style, "· " + item))
    return flow


def table(headers, rows, col_widths):
    head = [Paragraph(h, STYLES["cell_h"]) for h in headers]
    body = []
    for row in rows:
        body.append([Paragraph(str(c), STYLES["cell"]) for c in row])
    data = [head] + body
    t = Table(data, colWidths=col_widths, repeatRows=1)
    t.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, 0), NAVY),
                ("TEXTCOLOR", (0, 0), (-1, 0), WHITE),
                ("FONTNAME", (0, 0), (-1, 0), "DengB"),
                ("BACKGROUND", (0, 1), (-1, -1), WHITE),
                ("ROWBACKGROUNDS", (0, 1), (-1, -1), [WHITE, BAND]),
                ("VALIGN", (0, 0), (-1, -1), "TOP"),
                ("LEFTPADDING", (0, 0), (-1, -1), 6),
                ("RIGHTPADDING", (0, 0), (-1, -1), 6),
                ("TOPPADDING", (0, 0), (-1, -1), 5),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 5),
                ("GRID", (0, 0), (-1, -1), 0.3, LINE),
            ]
        )
    )
    return t


def header_footer(canvas, doc):
    canvas.saveState()
    width, height = A4
    if doc.page > 1:
        canvas.setFillColor(NAVY)
        canvas.rect(0, height - 12 * mm, width, 12 * mm, fill=1, stroke=0)
        canvas.setFillColor(WHITE)
        canvas.setFont("Deng", 8.5)
        canvas.drawString(18 * mm, height - 7.6 * mm, "小云宝  ·  云伴 CampusCare")
        canvas.drawRightString(width - 18 * mm, height - 7.6 * mm, "项目全方位讲解")
        canvas.setFillColor(GOLD)
        canvas.rect(0, height - 12.6 * mm, width, 1.2, fill=1, stroke=0)
        canvas.setFillColor(LINE)
        canvas.rect(18 * mm, 12 * mm, width - 36 * mm, 0.4, fill=1, stroke=0)
        canvas.setFillColor(MUTED)
        canvas.setFont("Deng", 8)
        canvas.drawString(18 * mm, 7.5 * mm, "样机阶段说明  ·  2026年9月20日")
        canvas.drawRightString(width - 18 * mm, 7.5 * mm, "第 %d 页" % doc.page)
    canvas.restoreState()


def cover_page():
    story = []
    story.append(Spacer(1, 38 * mm))
    story.append(p(STYLES["cover_kicker"], "CAMPUSCARE  ·  大学生陪伴终端"))
    story.append(p(STYLES["cover_title"], "小云宝"))
    story.append(p(STYLES["cover_sub"], "云伴 CampusCare"))
    story.append(Spacer(1, 6 * mm))
    story.append(p(STYLES["cover_sub"], "面向大学生的多模态情绪支持<br/>与英语四级陪练终端"))
    story.append(Spacer(1, 14 * mm))
    story.append(
        p(
            STYLES["cover_meta"],
            "项目全方位讲解　　产品 · 场景 · 特色 · 使用 · 商业价值 · 前景",
        )
    )
    story.append(p(STYLES["cover_meta"], "文档日期：2026年9月20日"))
    story.append(p(STYLES["cover_meta"], "适用：答辩介绍、团队对齐、校园试点说明"))
    story.append(Spacer(1, 22 * mm))
    story.append(
        p(
            STYLES["note"],
            "说明：本文依据当前可运行样机与竞赛定位整理。摄像头情绪为辅助线索，"
            "不构成心理或医学诊断，也不能替代辅导员、心理老师或医疗机构。"
            "涉及市场规模与收入的数字均为待验证框架，不作为承诺。",
        )
    )
    story.append(PageBreak())
    return story


def toc_page():
    items = [
        "一、一句话定位与执行摘要",
        "二、项目是什么：从硬件到云端的完整形态",
        "三、解决什么问题：大学生真实场景里的缺口",
        "四、产品特点与特色",
        "五、使用场景",
        "六、对大学生起到什么作用",
        "七、整体优势",
        "八、怎么使用",
        "九、技术架构与实现路径",
        "十、商业价值与可能模式",
        "十一、发展前景与下一步",
        "十二、边界、风险与关键假设",
    ]
    story = [p(STYLES["h1"], "目录")]
    story.extend(p(STYLES["toc"], item) for item in items)
    story.append(PageBreak())
    return story


def body():
    s = STYLES
    w = 174 * mm
    story = []

    story.append(p(s["h1"], "一、一句话定位与执行摘要"))
    story.append(
        p(
            s["lead"],
            "小云宝是一台可手持的语音 AI 终端：大学生按一下键就能说话，"
            "它用屏幕上的数字人物陪伴回应，也能切换成英语四级口语陪练；"
            "摄像头只用来辅助感知当下表情，不自动出声、不做诊断。",
        )
    )
    story.append(
        p(
            s["body"],
            "项目对外名称可写作「云伴 CampusCare」。它把嵌入式硬件、云端语音识别、"
            "大模型对话、语音合成和视觉情绪线索放进同一条链路，目标不是再做一个手机 App，"
            "而是放在宿舍桌上、随时能开口的低门槛陪伴设备。",
        )
    )
    story.append(p(s["h2"], "当前能讲清楚的三件事"))
    story.extend(
        bullets(
            s["bullet"],
            [
                "产品形态已从开发板堆叠走到可演示样机：屏幕人物、四键操作、云端对话、中英双模式。",
                "情绪支持与四级陪练共用同一套端云引擎，切换成本低，适合校园里“一个设备多种用途”。",
                "商业化方向明确：校园试点、心理/学习中心采购、内容订阅和硬件套件，但仍需用户验证与合规边界。",
            ],
        )
    )

    story.append(p(s["h1"], "二、项目是什么：从硬件到云端的完整形态"))
    story.append(
        p(
            s["body"],
            "小云宝不是纯软件，也不是只跑通语音的实验板。它是一套端云协同系统："
            "设备负责感知、显示和播放，云端负责听懂、思考和说话。用户感知到的是"
            "“一只会招手、会听、会答的小云朵”，背后是稳定的按键、网络、音频队列和安全边界。",
        )
    )
    story.append(p(s["h2"], "硬件组成"))
    story.append(
        table(
            ["模块", "作用", "用户能感觉到什么"],
            [
                ["ESP32-S3 N16R8", "主控、Wi-Fi、音频与屏幕驱动", "按键有响应，联网后能对话"],
                ["ST7789 彩屏 240×320", "数字人物、气泡、状态提示", "看得见它在听、在想、在说"],
                ["INMP441 麦克风", "采集 16 kHz 语音", "对着设备说话即可"],
                ["MAX98357A + 喇叭", "播放合成语音", "听到自然、带情绪的回复"],
                ["OV2640 摄像头", "低帧率画面，云端识别表情线索", "屏幕关怀更贴表情，不自动出声"],
                ["四枚实体键", "唤醒、中英、亮度、音量", "不依赖手机 App 完成主操作"],
                ["锂电池供电链路", "可离开电脑独立使用", "宿舍、教室都能开机"],
            ],
            [38 * mm, 58 * mm, 78 * mm],
        )
    )
    story.append(p(s["caption"], "表 1　小云宝硬件组成与用户感知"))

    story.append(p(s["h2"], "软件与云端在做什么"))
    story.append(
        p(
            s["body"],
            "设备把麦克风音频送到云端中转，云端完成语音识别（ASR）、千问对话、"
            "CosyVoice 语音合成，再把压缩音频送回设备播放。视觉链路把 JPEG 小图上传，"
            "由云端做人脸与情绪线索分析，结果用于屏幕关怀文案和对话时的语气，"
            "而不是用来“判定你有什么病”。",
        )
    )
    story.extend(
        bullets(
            s["bullet"],
            [
                "中文陪伴：共情、接话、给能马上做的小建议，对话尽量连贯。",
                "英语陪练：全程英语口语回应，可改错、给四级词块、追问同一话题。",
                "无网提示：没连上 Wi-Fi 时，页面直接显示「无网络连接」。",
                "待机省电：长时间不唤醒会降低屏幕亮度，隔约五分钟才抓一张照片。",
            ],
        )
    )

    story.append(p(s["h1"], "三、解决什么问题：大学生真实场景里的缺口"))
    story.append(
        p(
            s["body"],
            "大学生并不缺少信息，缺的是“愿意开口的那一下”。心理中心预约有门槛，"
            "朋友圈不便说真话，英语口语又常常只能对着手机默读。小云宝对准的是这两类"
            "高频、低危但仍被耽误的需求：日常情绪疏导，以及四级听说开口练习。",
        )
    )
    story.append(
        table(
            ["痛点", "常见替代方案", "现有方案的不足", "小云宝的切入"],
            [
                [
                    "压力大却不好意思求助",
                    "忍着、刷手机、找熟人倾诉",
                    "夜间难预约，开口成本高",
                    "按键即说，低门槛陪伴和转介提醒",
                ],
                [
                    "英语开口少、反馈慢",
                    "网课、单词 App、自学跟读",
                    "缺少即时对话和纠错",
                    "实体设备上的四级口语陪练",
                ],
                [
                    "情绪被误判或过度解读",
                    "测题、表情 App、自我诊断",
                    "易贴标签，缺少边界",
                    "表情只作线索，文本为主，明确不诊断",
                ],
                [
                    "手机场景太容易分心",
                    "微信语音、AI 聊天 App",
                    "通知打断，学习时难以专注",
                    "独立终端，使用意图更单纯",
                ],
            ],
            [36 * mm, 42 * mm, 48 * mm, 48 * mm],
        )
    )
    story.append(p(s["caption"], "表 2　问题、替代方案与产品切入点"))

    story.append(p(s["h1"], "四、产品特点与特色"))
    story.append(p(s["h2"], "1. 端云协同，而不是“板上硬跑大模型”"))
    story.append(
        p(
            s["body"],
            "识别、理解和发音放在云端，设备专注采集、显示和播放。这样成本可控、"
            "音色和模型可升级，同时样机已经能做连续对话，而不是一次性问答 Demo。",
        )
    )
    story.append(p(s["h2"], "2. 看得见的陪伴，而不只是一个喇叭"))
    story.append(
        p(
            s["body"],
            "屏幕有招手、聆听动画和聊天气泡，文字与声音尽量同步。用户知道它在听，"
            "也知道它在说哪一句。这对降低“对着空气说话”的别扭感很重要。",
        )
    )
    story.append(p(s["h2"], "3. 多模态但有边界"))
    story.append(
        p(
            s["body"],
            "摄像头识别到的开心、难过等线索，优先变成屏幕关怀文字；"
            "用户开口后，才把当下表情带进这一轮对话。误判不会让设备自己循环出声，"
            "也不会因为一张脸就下诊断。这是项目刻意保留的产品纪律。",
        )
    )
    story.append(p(s["h2"], "4. 一套硬件，两种校园刚需"))
    story.append(
        p(
            s["body"],
            "K2 一切换就是英语陪练：改一句能跟读的句子、给一个四级词块、或追问同一话题。"
            "情绪支持和语言训练共用麦克风、喇叭、网络和模型，比做两个互不相关的作品更像产品。",
        )
    )
    story.append(p(s["h2"], "5. 为宿舍场景设计的操作"))
    story.extend(
        bullets(
            s["bullet"],
            [
                "四键完成唤醒、打断、中英、亮度、音量，不需要先打开手机。",
                "手机热点配网，换宿舍或换网络不必重新烧录固件。",
                "长按可重连 Wi-Fi；没网时页面直接告知，避免用户以为设备坏了。",
                "长时间不用进入低亮度待机，兼顾陪伴在场和耗电。",
            ],
        )
    )

    story.append(p(s["h1"], "五、使用场景"))
    story.append(
        table(
            ["场景", "谁在用", "典型行为", "产品如何接住"],
            [
                [
                    "宿舍深夜",
                    "复习后情绪低落的学生",
                    "不想打电话，但想有人听一句",
                    "按键唤醒，共情后给一个 60 秒可做的小动作",
                ],
                [
                    "四级备考",
                    "口语薄弱、怕开口的同学",
                    "跟读、问答、把中文意思用英语说出来",
                    "英语模式陪练，改错、给词、继续同一话题",
                ],
                [
                    "心理中心候诊/预沟通",
                    "预约前后紧张的学生",
                    "先整理自己想说什么",
                    "低门槛倾诉，必要时提示校园求助资源",
                ],
                [
                    "实验室/竞赛展台",
                    "评委、来宾、新生",
                    "短时间完整体验听—想—说",
                    "实体样机演示中英切换和屏幕人物",
                ],
                [
                    "英语角或自学桌",
                    "想练听说但不想被围观的人",
                    "单独对设备说几句",
                    "私密、可打断、可反复练习",
                ],
            ],
            [32 * mm, 40 * mm, 50 * mm, 52 * mm],
        )
    )
    story.append(p(s["caption"], "表 3　主要使用场景"))

    story.append(p(s["h1"], "六、对大学生起到什么作用"))
    story.append(p(s["h2"], "情绪侧：把“说不出口”变成“先说出来”"))
    story.append(
        p(
            s["body"],
            "作用不是治疗，而是降低第一句话的成本。设备可以接住考试压力、人际别扭、"
            "想家、倦怠这类日常表达，帮使用者把一团情绪拆成下一件能做的小事。"
            "如果出现明确的高风险表达，产品定位是提醒和转介校园资源，而不是继续闲聊或自行处理。",
        )
    )
    story.append(p(s["h2"], "学习侧：把“默读单词”变成“真的开口”"))
    story.append(
        p(
            s["body"],
            "四级备考里最容易被放弃的是说。小云宝用英语回答、纠正、给可跟读的句子，"
            "让练习发生在真实对话节奏里。学生可以夹杂中文，设备仍用英语接住，"
            "这更接近校园里真实的中英混用习惯。",
        )
    )
    story.append(p(s["h2"], "素养侧：理解 AI 的边界"))
    story.append(
        p(
            s["body"],
            "使用过程本身也是一次媒介素养教育：AI 可以陪伴和陪练，但不能替代专业帮助；"
            "摄像头可以辅助，但默认不保存原图、不采集学号姓名。"
            "对计算机、英语、心理、工业设计等专业的学生，它还是一个可拆、可改、可答辩的完整作品。",
        )
    )

    story.append(p(s["h1"], "七、整体优势"))
    story.append(
        table(
            ["维度", "优势", "需要诚实说明的限制"],
            [
                [
                    "形态",
                    "独立终端，使用意图清晰，比手机 App 更适合“放下屏幕、开口说话”",
                    "便携性和续航仍取决于外壳、电池和现场电源质量",
                ],
                [
                    "交互",
                    "按键 + 屏幕人物 + 语音，反馈完整",
                    "英语识别受环境和口音影响，需要继续调收音窗口",
                ],
                [
                    "技术",
                    "端云分工清楚，模型可升级，固件可迭代",
                    "依赖网络和云服务，断网时不能完整对话",
                ],
                [
                    "伦理",
                    "明确不诊断、不替代老师、视觉只作线索",
                    "风险识别与转介流程仍要和学校制度对接",
                ],
                [
                    "成本",
                    "模块化开发板方案，适合小批量试点和教学复现",
                    "量产需要结构件、认证、供应链，不是当前样机成本可直接外推",
                ],
                [
                    "场景契合",
                    "同时覆盖心理健康“开口难”和四级“开口少”",
                    "两类场景的运营方不同，进校路径要分开验证",
                ],
            ],
            [28 * mm, 73 * mm, 73 * mm],
        )
    )
    story.append(p(s["caption"], "表 4　优势与限制对照（避免把样机能力写成已规模化能力）"))

    story.append(p(s["h1"], "八、怎么使用"))
    story.append(p(s["h2"], "第一次开机"))
    story.extend(
        bullets(
            s["bullet"],
            [
                "接通电源。若还没有 Wi-Fi，屏幕页面直接提示「无网络连接」。",
                "设备会开出热点 XiaoYunBao-XXXX，密码为配网页提示的默认口令。",
                "手机连上热点后，浏览器打开 192.168.4.1，选择宿舍 Wi-Fi 或填写手机热点。",
                "配网成功后即可使用。之后换网络：长按 K1 重连，或再次进入配网。",
            ],
        )
    )
    story.append(p(s["h2"], "四个按键"))
    story.append(
        table(
            ["按键", "短按", "长按"],
            [
                ["K1 唤醒键", "开始听；听的时候再按表示说完。正在说话时可打断。没听到内容则回到待机。", "重连 Wi-Fi；若从未配网则进入手机配网"],
                ["K2 模式键", "中文陪伴 / 英语陪练切换", "静音 / 取消静音"],
                ["K3", "亮度降低", "音量降低（按住可连减）"],
                ["K4", "亮度升高", "音量升高（按住可连加）。上电和烧录时不要按住"],
            ],
            [28 * mm, 78 * mm, 68 * mm],
        )
    )
    story.append(p(s["caption"], "表 5　实体按键用法（与当前样机一致）"))

    story.append(p(s["h2"], "中文陪伴怎么聊"))
    story.extend(
        bullets(
            s["bullet"],
            [
                "短按 K1，看到聆听动画后开始说话。",
                "说完再按一次 K1，或等它判断你已经说完。",
                "屏幕出现回复气泡，喇叭同步播放。",
                "可以连续聊。长时间不说话会结束本轮，回到“请唤醒我开始交流”。",
                "摄像头若识别到表情，屏幕会给出关怀文字；只有你开口后，才把表情带进这一轮回答。",
            ],
        )
    )
    story.append(p(s["h2"], "英语陪练怎么用"))
    story.extend(
        bullets(
            s["bullet"],
            [
                "短按 K2 进入英语模式。",
                "用英语或中文说都可以，设备用英语回答。",
                "它可能改你的句子让你跟读，给一个四级词，或问一个不跑题的问题。",
                "说完整一句，不要只吐一个语气词；环境太吵时靠近麦克风。",
            ],
        )
    )
    story.append(p(s["h2"], "使用时请记住的三句话"))
    story.extend(
        bullets(
            s["bullet"],
            [
                "它是陪伴和陪练，不是心理医生。",
                "摄像头画面默认用于现场分析，不作为身份识别，也不保存学号姓名。",
                "真的很难受、或出现伤害自己/他人的念头时，请找身边的人、辅导员或学校心理中心。",
            ],
        )
    )

    story.append(p(s["h1"], "九、技术架构与实现路径"))
    story.append(
        p(
            s["body"],
            "主链路可以写成一句：麦克风 16 kHz 音频 → 云端识别成文字 → 大模型生成回复 → "
            "语音合成 → 设备解码播放；同时可选把摄像头 JPEG 送到云端，得到情绪标签。"
            "设备侧负责按键、屏幕、待机、断网提示和音频队列，云侧负责密钥、模型和视觉推理。",
        )
    )
    story.append(
        table(
            ["环节", "当前做法", "设计意图"],
            [
                ["听", "云端流式 ASR，英语加长收音和句末确认", "少把话切断，少把杂音当成一句话"],
                ["想", "千问流式生成，带对话历史和视觉线索", "回答接得上上一句，而不是每次自我介绍"],
                ["说", "CosyVoice 合成，去掉情绪控制标签再发音", "标签只驱动表情和语气，不会被读出来"],
                ["看", "QVGA JPEG 上传，云端人脸/情绪线索", "辅助关怀，失败时语音仍可用"],
                ["显", "离屏画布刷新气泡与动画分区", "人物动画不把对话框闪掉"],
            ],
            [28 * mm, 78 * mm, 68 * mm],
        )
    )
    story.append(p(s["caption"], "表 6　听—想—说—看—显 的实现要点"))
    story.append(
        p(
            s["body"],
            "对评委或合作方，这条链路的意义在于可验证：唤醒是否稳定、首句是否出声、"
            "断网是否有提示、拔掉摄像头后对话是否仍可用。竞争力来自完整闭环，而不是功能清单长短。",
        )
    )

    story.append(p(s["h1"], "十、商业价值与可能模式"))
    story.append(
        p(
            s["body"],
            "商业价值首先来自“愿意付费或采购的场景”，而不是技术本身。"
            "小云宝同时碰到学校的学生支持职责，和学生自己的备考、开口练习需求。"
            "下面按对象分开看，避免把所有人写成同一个客户。",
        )
    )
    story.append(p(s["h2"], "可能的付费方"))
    story.append(
        table(
            ["对象", "为什么可能买", "更可能的形态", "验证时要看什么"],
            [
                [
                    "学校心理中心 / 学工",
                    "需要低门槛预沟通和转介，而不是替代咨询",
                    "试点设备 + 服务协议",
                    "学生是否真的开口、转介是否可执行",
                ],
                [
                    "外语学院 / 学习中心",
                    "口语练习缺少即时反馈",
                    "实验室或宿舍借还设备",
                    "练习次数、开口时长、续用意愿",
                ],
                [
                    "学生个人",
                    "想要一个不刷手机的陪练/陪伴物件",
                    "硬件销售 + 时长或内容订阅",
                    "愿付价格、每周使用频次",
                ],
                [
                    "教育硬件 / 内容伙伴",
                    "需要可落地的端侧交互外壳",
                    "ODM 套件或内容分成",
                    "模块成本和交付周期",
                ],
            ],
            [36 * mm, 48 * mm, 42 * mm, 48 * mm],
        )
    )
    story.append(p(s["caption"], "表 7　商业对象与验证指标（待实地验证）"))

    story.append(p(s["h2"], "收入结构框架（示例，非预测）"))
    story.append(
        p(
            s["note"],
            "以下只用于说明“钱可能从哪来”，不是财务预测。单价、转化率和成本需试点后回填。",
        )
    )
    story.extend(
        bullets(
            s["bullet"],
            [
                "硬件：整机或开发套件一次性收入。",
                "服务：按学期的云端识别/对话时长、内容更新、设备巡检。",
                "内容：四级题库、校园场景包、院系定制话术。",
                "项目制：单校试点、比赛或实验室采购。",
            ],
        )
    )
    story.append(
        p(
            s["body"],
            "一个便于讨论的单位经济问题是：一台设备服务多少名学生、每周被拿起几次、"
            "云端每分钟成本是多少。答不上这三问，就还不能谈规模化利润。",
        )
    )

    story.append(p(s["h1"], "十一、发展前景与下一步"))
    story.append(p(s["h2"], "为什么现在做有窗口"))
    story.extend(
        bullets(
            s["bullet"],
            [
                "大模型语音能力已经能支撑连续中文对话和英语短句陪练，端侧不必自研基础模型。",
                "高校对学生心理健康和外语能力都有考核与服务压力，需要可展示、可试点的工具。",
                "学生对“又一个手机 App”疲劳，独立设备反而形成差异化。",
            ],
        )
    )
    story.append(p(s["h2"], "三条演进路线"))
    story.append(
        table(
            ["阶段", "目标", "完成时应该能证明什么"],
            [
                [
                    "现在：样机与答辩",
                    "稳定演示中英双模式、断网提示、情绪线索不越界",
                    "90 秒内跑完听—答—切换，现场不崩溃",
                ],
                [
                    "下一阶段：校园小样本",
                    "10～30 名自愿学生匿名试用",
                    "开口次数、易用性、是否觉得被理解、英语是否更敢说",
                ],
                [
                    "再往后：产品化",
                    "外壳、认证、隐私合规、内容运营",
                    "单校可交付、可维护、可关闭摄像头仍能用",
                ],
            ],
            [42 * mm, 66 * mm, 66 * mm],
        )
    )
    story.append(p(s["caption"], "表 8　阶段目标。样本量是建议，不是已完成结果。"))
    story.append(
        p(
            s["body"],
            "前景取决于两件事是否成立：学生是否反复使用，以及学校是否接受“陪伴不等于咨询”。"
            "成立，就可以做成校园服务终端；不成立，它仍然是很好的教学与竞赛作品，"
            "但不应提前写成全国市场故事。",
        )
    )

    story.append(p(s["h1"], "十二、边界、风险与关键假设"))
    story.append(p(s["h2"], "必须守住的边界"))
    story.extend(
        bullets(
            s["bullet"],
            [
                "不宣称诊断、治愈、替代心理老师或医生。",
                "摄像头结果只是表情线索，可关闭、可失效降级。",
                "不把姓名、学号、人脸原图作为默认采集项。",
                "高风险内容转向人工和校园资源，而不是用安慰话术带过。",
            ],
        )
    )
    story.append(p(s["h2"], "主要风险"))
    story.append(
        table(
            ["风险", "可能表现", "缓解"],
            [
                ["网络与云依赖", "没网就不能对话", "页面提示无网络；后续可考虑关键提示离线化"],
                ["误识别", "英语听成杂音，表情误判", "加长英文收音、噪声门；表情锁定一轮再切换"],
                ["隐私与伦理", "被理解为监视或心理测评", "默认少存、可关闭镜头、文案统一"],
                ["硬件一致性", "供电噪声、按键抖动、摄像发热", "电源与按键规范、连续运行测试"],
                ["进校销售周期", "试点容易、采购慢", "先借还/实验室，再谈学期服务"],
            ],
            [32 * mm, 58 * mm, 84 * mm],
        )
    )
    story.append(p(s["caption"], "表 9　主要风险与缓解"))

    story.append(p(s["h2"], "关键假设（尚未被大规模数据证实）"))
    story.extend(
        bullets(
            s["bullet"],
            [
                "假设 A：学生更愿意对独立设备开口，而不是对手机 App。需对照试验。",
                "假设 B：即时口语反馈能提高四级开口频次。需前后测。",
                "假设 C：学校愿意为“预沟通/陪练”付费，而不是只要免费体验。需访谈采购决策链。",
                "假设 D：在可接受成本下，云端时延和识别率能达到宿舍可用。需现场网络实测。",
            ],
        )
    )

    story.append(p(s["h1"], "结语"))
    story.append(
        p(
            s["lead"],
            "小云宝要证明的不是“我们接入了大模型”，而是：在大学宿舍里，"
            "一台安静的小设备能够让人把第一句话说出来，并把下一句英语说完整；"
            "同时清楚地知道自己不是医生、不是监考、也不是监视器。",
        )
    )
    story.append(
        p(
            s["body"],
            "把样机讲完整、把边界讲清楚、把验证做扎实，这就是当前最大的商业价值和竞赛价值。"
            "下一步不该是堆更多功能，而是让真实学生把它用过一周，留下可核对的记录。",
        )
    )
    story.append(Spacer(1, 8 * mm))
    story.append(
        p(
            s["note"],
            "文档版本：2026-09-20。对应样机能力包括：云端中文陪伴与英语陪练、屏幕情绪关怀、"
            "四键交互、手机配网、断网页面提示、待机抓拍。具体固件以设备串口版本号为准。",
        )
    )
    return story


def build():
    register_fonts()
    global STYLES
    STYLES = styles()
    doc = SimpleDocTemplate(
        str(OUTPUT),
        pagesize=A4,
        leftMargin=18 * mm,
        rightMargin=18 * mm,
        topMargin=18 * mm,
        bottomMargin=16 * mm,
        title="小云宝项目全方位讲解",
        author="小云宝项目组",
        subject="云伴 CampusCare 产品与商业说明",
    )
    story = []
    story.extend(cover_page())
    story.extend(toc_page())
    story.extend(body())
    doc.build(story, onFirstPage=header_footer, onLaterPages=header_footer)
    print(OUTPUT)


if __name__ == "__main__":
    build()
