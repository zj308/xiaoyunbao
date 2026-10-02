# -*- coding: utf-8 -*-
"""Generate the detailed XiaoYunBao finished-product handbook."""

from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER, TA_JUSTIFY
from reportlab.lib.pagesizes import A4
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
OUTPUT = ROOT / "小云宝完整使用手册.pdf"
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
            "kicker", fontName="DengB", fontSize=11, textColor=GOLD,
            alignment=TA_CENTER, spaceAfter=8,
        ),
        "title": ParagraphStyle(
            "title", fontName="SimHei", fontSize=28, leading=36,
            textColor=NAVY, alignment=TA_CENTER, spaceAfter=8,
        ),
        "sub": ParagraphStyle(
            "sub", fontName="Deng", fontSize=12.5, leading=20,
            textColor=TEAL, alignment=TA_CENTER, spaceAfter=4,
        ),
        "meta": ParagraphStyle(
            "meta", fontName="Deng", fontSize=10, leading=16,
            textColor=MUTED, alignment=TA_CENTER, spaceAfter=6,
        ),
        "h1": ParagraphStyle(
            "h1", fontName="SimHei", fontSize=15, leading=22,
            textColor=NAVY, spaceBefore=14, spaceAfter=6,
        ),
        "h2": ParagraphStyle(
            "h2", fontName="DengB", fontSize=12.5, leading=18,
            textColor=TEAL, spaceBefore=8, spaceAfter=4,
        ),
        "body": ParagraphStyle(
            "body", fontName="Deng", fontSize=11, leading=18,
            textColor=INK, alignment=TA_JUSTIFY, spaceAfter=6,
        ),
        "step": ParagraphStyle(
            "step", fontName="Deng", fontSize=11, leading=18,
            textColor=INK, leftIndent=6, spaceAfter=3,
        ),
        "cell": ParagraphStyle(
            "cell", fontName="Deng", fontSize=9.5, leading=14.5, textColor=INK,
        ),
        "cell_h": ParagraphStyle(
            "cell_h", fontName="DengB", fontSize=9.5, leading=14.5, textColor=WHITE,
        ),
        "example": ParagraphStyle(
            "example", fontName="Deng", fontSize=11, leading=18,
            textColor=INK, leftIndent=8, spaceAfter=4,
        ),
    }


S = {}


def P(style, text):
    return Paragraph(text, S[style])


def table(headers, rows, widths):
    data = [[P("cell_h", h) for h in headers]]
    for row in rows:
        data.append([P("cell", c) for c in row])
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
    w, h = A4
    canvas.setFillColor(NAVY)
    canvas.rect(0, h - 12 * mm, w, 12 * mm, fill=1, stroke=0)
    canvas.setFillColor(WHITE)
    canvas.setFont("Deng", 9)
    canvas.drawString(18 * mm, h - 7.5 * mm, "小云宝  完整使用手册")
    canvas.setFillColor(GOLD)
    canvas.rect(0, 0, w, 10 * mm, fill=1, stroke=0)
    canvas.setFillColor(NAVY)
    canvas.setFont("Deng", 9)
    canvas.drawRightString(w - 16 * mm, 4 * mm, str(doc.page))
    canvas.restoreState()


def build():
    story = []
    story.append(Spacer(1, 18 * mm))
    story.append(P("kicker", "云伴  ·  校园陪伴"))
    story.append(P("title", "小云宝完整使用手册"))
    story.append(P("sub", "一台会看表情、会陪你说话、也会陪你练英语的桌面伙伴"))
    story.append(P("meta", "成品使用说明    固件 2026-09-25-companion-v139"))
    story.append(Spacer(1, 6 * mm))

    story.append(P("h1", "一、小云宝是什么"))
    story.append(P("body", "小云宝是放在书桌或床头的校园陪伴。屏幕上住着一个云朵小人，名字叫云伴。它看着你的表情，在画面里给你一句轻轻的关怀；你按一下唤醒键，它才开口，用中文陪你把这件事说完。想练口语时，按一下语言键，它换成英语陪练，听得懂你的中文和英文，回答只用清楚的英语。"))
    story.append(P("body", "它不是一个一直播报的音箱，也不是一块只显示文字的屏。平时它安静地待在主页上，小人会招手；你开始说话之后，声音和屏幕上的回答一起出现，并且顺着上一句往下聊。一轮说完，它接着听下一句。你停下来之后，它再回到等待唤醒的样子。"))

    story.append(P("h1", "二、你会看到和听到的最终效果"))
    story.append(P("body", "打开侧面开关并连上已经保存的网络后，屏幕是陪伴主页。最上面是标题「云伴」。中间是云朵小人，手臂一起一落，像在打招呼。下面有一条圆角气泡，写着「请唤醒我开始交流」。这时房间里是安静的，它不会自己出声。"))
    story.append(P("body", "你坐在它面前，表情变了，气泡里的关怀句子会跟着换。看起来轻松时，句子也轻快一点；看起来低落时，句子会先陪着你，再停在这一件事上。这些句子只出现在屏幕上。"))
    story.append(P("body", "短按唤醒键之后，气泡立刻变成「正在倾听」，小人换成聆听的动作。你说出一句完整的话，它确认你说完，就用声音回答，同一段文字出现在屏幕上。回答顺着你刚才的话走，带着温度，篇幅适合听完。这一轮结束后麦克风继续开着，你可以直接说下一句。大约半分钟没有人说话，对话收束，画面回到「请唤醒我开始交流」。"))
    story.append(P("body", "再过大约两分钟没有人操作，屏幕柔和地变暗，进入待机。待机时它仍会隔一段时间看一眼画面，发现你的表情之后，再回到亮着的关怀页面。"))
    story.append(P("body", "英语模式下，画面换成口语练习。你的话和它的英语回答分成气泡，上下排列，白底黑字，句子按词语换行。它会把你的意思接住，用英语说回来，并留下一个可以继续的小问题、一个更自然的说法，或一个刚刚好用上的词。"))

    story.append(P("h1", "三、机身上的每一个部分"))
    story.append(P("h2", "侧面开关"))
    story.append(P("body", "机身侧面有一枚船型翘板开关，用来打开和关闭整机。拨到开，屏幕亮起，小人出现，已保存的网络开始连接。拨到关，屏幕和声音一起停下。日常使用只动这枚开关。"))
    story.append(P("h2", "屏幕"))
    story.append(P("body", "屏幕竖着放。排线伸出的那一头是下面，标题、小人、气泡和所有提示都按这个方向阅读。陪伴主页分成三层：顶上是「云伴」，中间是会动的小人，下面是状态或回答。英语页面则把对话放在画面下半部，你的话和它的回答各自成泡。"))
    story.append(P("h2", "摄像头"))
    story.append(P("body", "摄像头在屏幕上方，用来看你的表情。表情会变成屏幕上的关怀句子，也会在你开口之后，让回答更贴着你此刻的心情。它在你说话时仍会看一眼画面，好让这一轮对话知道你现在的状态。"))
    story.append(P("h2", "听和说"))
    story.append(P("body", "机内有麦克风和喇叭。唤醒之后，麦克风听你说话；回答准备好之后，喇叭把这句话说出来，屏幕同时把字显示出来。长按语言键可以静音，屏幕会显示「已静音」；再长按一次，声音回来。"))

    story.append(P("h1", "四、四个按键"))
    story.append(P("body", "机身有四枚按键。短按是点一下就松开，长按大约按住一秒多。屏幕顶部会用一条短暂的提示告诉你刚刚发生了什么。"))
    story.append(table(
        ["按键", "短按时", "长按时"],
        [
            ["K1 唤醒键", "开始倾听。它正在说时，打断并改听你的新话。正在听时再按一下，结束这一句。", "打开配网热点，重新选择 Wi-Fi。"],
            ["K2 语言键", "中文陪伴和英语口语互换。", "静音，或取消静音。"],
            ["K3", "亮度降低一档。", "按住不放，音量连续减小。"],
            ["K4", "亮度提高一档。", "按住不放，音量连续加大。"],
        ],
        [32 * mm, 68 * mm, 68 * mm],
    ))
    story.append(Spacer(1, 3 * mm))
    story.append(P("body", "亮度从屏幕亮起时的大约七成开始，每按一次变化一成。音量从大约三成开始，长按 K3 或 K4 会一档一档变化。顶部会显示「亮度:70%」「音量:30%」或「已静音」。"))

    story.append(P("h1", "五、打开电源，连上网络"))
    story.append(P("step", "1. 把侧面开关拨到开。屏幕亮起，先进入陪伴主页。"))
    story.append(P("step", "2. 如果这台小云宝以前已经选过网络，它会自己连上那个网络，小人开始招手，气泡写着「请唤醒我开始交流」。"))
    story.append(P("step", "3. 第一次使用，或想换成另一个 Wi-Fi 时，长按唤醒键 K1。"))
    story.append(P("step", "4. 屏幕改成配网页面，上面有四行清楚的字：连接网络、热点名称 XiaoYunBao- 加四位区分号、密码 xiaoyunbao、以及地址 192.168.4.1。"))
    story.append(P("step", "5. 用手机连接这个热点，密码是 xiaoyunbao。连上后用浏览器打开 192.168.4.1。"))
    story.append(P("step", "6. 在页面里选出要使用的 Wi-Fi，填入密码，保存。屏幕会显示正在连接，连好之后回到陪伴主页。"))
    story.append(P("body", "原来保存的网络会留在机器里。只有当你在手机页面里保存了一个新网络，它才改用新的。这样换网络时，原来的密码仍然在，开机后还能回到熟悉的网络。"))
    story.append(P("body", "还没有连上网络时，陪伴页面直接显示「无网络连接」。这时按唤醒键，屏幕仍停留在这个提示上，等网络连上后再开始说话。"))

    story.append(P("h1", "六、陪伴模式：从坐下来到说完一轮"))
    story.append(P("h2", "安静陪伴"))
    story.append(P("body", "这是开机后的默认状态。你坐下、看书、发呆，它都在主页上。小人重复招手。摄像头看到你的表情后，气泡换成一句对应的关怀，过一会儿再换成同一情绪里的下一句。你不按唤醒键，喇叭保持安静。"))
    story.append(P("h2", "唤醒"))
    story.append(P("body", "短按 K1。屏幕顶部闪过「已唤醒」，气泡马上变成「正在倾听」，小人改成聆听动作。你开始说话。它会把你这一句听完整，再进入回答。"))
    story.append(P("h2", "回答"))
    story.append(P("body", "回答用口语说出来，同时写在屏幕上。句子短，语气像一个住在屏幕里的朋友。它记得这一轮里你刚说过的事，也记得自己刚回答过的话，所以下一句会接上去，而不是重新介绍自己。你的原话留在对话过程里，陪伴页的气泡用来显示它的状态和它说的话。"))
    story.append(P("h2", "接着说"))
    story.append(P("body", "第一句回答结束后，麦克风自己打开。你可以直接说下一句。这样可以把一件事分成几句慢慢讲，例如先说今天很累，再说下午的课，再说你其实想被听完。每一句它都顺着前面的内容回应。"))
    story.append(P("h2", "打断和结束"))
    story.append(P("body", "它正在说的时候，再短按一次 K1，当前这句话停下，气泡回到「正在倾听」，你可以马上说新的内容。它正在听的时候再短按一次，表示这一句说完了：听到了就回答，这一拍没有声音就回到唤醒页面。大约 30 秒没有新的话，这一轮对话自然结束。"))
    story.append(P("h2", "待机"))
    story.append(P("body", "对话结束后，如果大约两分钟都没有按键，也没有新的唤醒，屏幕亮度柔和地降下来。它仍会隔大约五分钟看一次画面。看到你的表情后，屏幕重新亮起，关怀句子回到页面上。你再按一次 K1，又可以开始说话。"))

    story.append(P("h1", "七、陪伴模式的例子"))
    story.append(P("h2", "例子一：晚上回到宿舍"))
    story.append(P("example", "你打开侧面开关。小云宝连上宿舍的 Wi-Fi，小人向你招手，气泡写着「请唤醒我开始交流」。你皱着眉坐下放下包，屏幕上的句子变成一句轻轻的陪伴，喇叭仍然不出声。"))
    story.append(P("example", "你短按 K1，说：「我今天有点累。」气泡先是「正在倾听」，随后它用较慢的声音说它在，愿意听你讲。文字出现在屏幕上。"))
    story.append(P("example", "你接着说：「下午还有课，不太想去。」不用再按键。它接住「下午的课」，问你是想歇一歇，还是想把这节课对付过去。你回答之后，它给出一个马上能做的小步骤，然后停住，等你要不要继续。"))
    story.append(P("example", "你沉默下来。大约半分钟后，画面回到「请唤醒我开始交流」。你去洗漱，大约两分钟后屏幕变暗。"))
    story.append(P("h2", "例子二：想把一件委屈说完"))
    story.append(P("example", "你按 K1，说：「今天组会被说了。」它先承认这件事让人难受，再问是哪一句话让你一直记得。"))
    story.append(P("example", "它还在说的时候，你突然想改口，又按了一次 K1。它立刻停住，重新听。你说：「算了，我不想讲组会了，我想说晚饭没吃。」它放下组会，改问你现在想吃点什么，或者要不要先喝一口水。"))
    story.append(P("h2", "例子三：换一个网络"))
    story.append(P("example", "你从家里回到学校，长按 K1。屏幕变成配网页：热点 XiaoYunBao- 加四位号码，密码 xiaoyunbao，地址 192.168.4.1。手机连上热点，打开这个地址，选宿舍 Wi-Fi 并保存。连好后小人回来，气泡重新写上「请唤醒我开始交流」。家里的那个网络仍保存在机器里，下次回家开机还会去找它；只有你再次在页面里保存别的网络，才会换成新的。"))

    story.append(P("h1", "八、英语口语模式"))
    story.append(P("body", "短按 K2，从小云宝的中文陪伴走进英语口语。如果它正在说话，会先把这一句收住，再换画面，这样麦克风可以干净地交给下一轮。换成英语后，先有一句英语提示音，页面变成口语练习，下面是对话区。"))
    story.append(P("body", "你可以用英语说，也可以用中文说，还可以中英夹在一起。它听懂你的意思之后，回答全部用自然、清楚的英语。你说中文时，它会先把你的意思说成一句简单英语，再带着你往下说。回答不长，适合跟着读、跟着接。"))
    story.append(P("body", "每一轮英语回答会做一件刚刚好的事：把你的句子收成一句更自然的英语，或送你一个能马上用的词和极短的例子，或问一个留在同一话题里的问题。画面上，你的话在一侧气泡，它的英语在白底黑字的气泡里，长句按词语换到下一行。"))
    story.append(P("body", "你说完一句，稍停一下，它再把整句收下并开始回答。句中换气的短暂停顿仍然算在这一句里面。回答结束后，你可以继续说下一句。再短按一次 K2，画面和语言一起回到中文陪伴。"))

    story.append(P("h1", "九、英语模式的例子"))
    story.append(P("h2", "例子一：用英语问天气"))
    story.append(P("example", "你短按 K2，听到英语提示音。然后说：What's the weather like tomorrow?"))
    story.append(P("example", "屏幕先显示出你的句子。它用英语告诉你明天的天气，并问你明天有没有安排，例如：Tomorrow looks sunny. Do you have any plans? 白底上的黑字按词换行，你可以一边听一边看。"))
    story.append(P("h2", "例子二：用中文起头，用英语接下去"))
    story.append(P("example", "你说：「我想请一天假。」它理解这是请假，然后用英语把这句话给你：I want to ask for a day off. 接着它问你要向谁请，或者要请哪一天，例如：Who will you ask? 你可以用英语接 Who is my teacher，它再帮你说得更顺：I will ask my teacher。"))
    story.append(P("h2", "例子三：把一句不顺的英语说顺"))
    story.append(P("example", "你说：I very like this class, but tomorrow I no go. 它先把意思收成一句自然的英语：I really like this class, but I can't go tomorrow. 然后只留一个小问题：What will you do instead? 你回答之后，对话仍停在「明天不去上课」这件事上。"))
    story.append(P("h2", "例子四：练完回到陪伴"))
    story.append(P("example", "你再短按 K2。英语页面收起，云朵小人和「云伴」回到屏幕上。之后短按 K1，又是中文陪伴。夜里如果想让它安静，长按 K2，顶部显示「已静音」。"))

    story.append(P("h1", "十、表情怎样进入陪伴"))
    story.append(P("body", "摄像头一直在看你的表情，并把表情分成可以陪伴的几类，例如开心、难过、害羞、惊讶，以及需要先被接住的生气或害怕。在你还没唤醒它的时候，这些表情只决定屏幕上那一句关怀怎么写、按什么顺序换。"))
    story.append(P("body", "你开口之后，这一刻的表情会陪着你的话一起交给回答。所以你说「我没事」而表情很低落时，它的声音和用词会先靠过来，再继续你说的那件事。回答里用来驱动表情和语气的标记只留在机器内部，说出来和写在屏幕上的，都是给人听、给人看的正文。"))

    story.append(P("h1", "十一、一天里可以怎样用"))
    story.append(P("body", "早上出门前，打开开关。它连上家里的网络，你按一下 K1，说今天第一节课的心情。它陪你说两句，你关上网页、带上门。侧面开关可以留到你确定今天暂时不用它时再拨到关。"))
    story.append(P("body", "中午换教室或换宿舍，长按 K1，用手机页面把网络换成身边这个。保存之后，主页回来，你就可以继续按 K1 说话。"))
    story.append(P("body", "下午想练一段口语，短按 K2。用中文讲你想说的事，让它变成英语；再用英语接它的问题。练完短按 K2 回来。"))
    story.append(P("body", "晚上如果只想让它陪着，不必一直按键。你坐在灯下，它看你的表情，在屏幕上换句子。你累了，大约两分钟后它自己把光调暗。第二天打开开关，或在待机里按一次 K1，又从「请唤醒我开始交流」开始。"))

    story.append(P("h1", "十二、亮度、音量和静音"))
    story.append(P("body", "觉得屏幕太亮，短按 K3，每按一次暗一档，顶部显示当前亮度。想更亮，短按 K4。夜间可以把它留在较暗的一档，字和小人仍然在。"))
    story.append(P("body", "想把声音调小，按住 K3，音量会连续下降，顶部显示当前音量。按住 K4 则连续升高。完全不想出声时，长按 K2，进入静音；再长按一次，恢复原来的音量。静音时屏幕上的句子、动画和对话文字都还在，只是喇叭不出声。"))

    story.append(P("h1", "十三、你和它相处时的节奏"))
    story.append(table(
        ["时刻", "你做什么", "屏幕", "声音"],
        [
            ["刚开机，网络已在", "看它一眼", "云伴、招手的小人、「请唤醒我开始交流」", "安静"],
            ["表情变化，尚未唤醒", "坐在它面前", "关怀句子轮换", "安静"],
            ["想说话", "短按 K1，然后说", "「正在倾听」，然后是它的回答", "回答被说出来"],
            ["还想继续", "直接说下一句", "新的回答接上上一句", "继续说"],
            ["它话没说完你想改口", "再短按 K1", "回到「正在倾听」", "当前这句话停下"],
            ["半分钟没说话", "离开或发呆", "回到唤醒提示", "安静"],
            ["大约两分钟没操作", "让它自己待着", "亮度变暗", "安静"],
            ["想练英语", "短按 K2", "口语练习和对话气泡", "先有英语提示，再回答"],
            ["想换 Wi-Fi", "长按 K1，用手机打开 192.168.4.1", "热点名、密码和地址", "安静"],
            ["夜里", "长按 K2", "显示已静音", "没有声音"],
        ],
        [38 * mm, 48 * mm, 52 * mm, 36 * mm],
    ))

    story.append(P("h1", "十四、整机用了哪些元件"))
    story.append(P("body", "小云宝由一块主控、一块屏幕、一个摄像头、一只麦克风、一块功放、一张存储卡、四枚按键和一条电池供电链路组成。它们装在同一套板上，通过排针和排线连到 ESP32-S3。"))
    story.append(table(
        ["元件", "在机器里的角色"],
        [
            ["ESP32-S3 N16R8 开发板", "主控。16MB 闪存存放程序，8MB OPI PSRAM 存放屏幕动画和画面。自带 Type-C，用于供电和刷机。"],
            ["ST7789 彩色屏，240×320", "显示云伴、招手和聆听动画、气泡、配网文字。"],
            ["OV2640 摄像头模组", "采集人脸画面。模组自带振荡器，主控不另供时钟。"],
            ["INMP441 麦克风", "把你的声音收成数字音频。"],
            ["MAX98357A 功放和喇叭", "把云端返回的语音放出来。喇叭两根线只接功放的两个输出端。"],
            ["TF 卡", "存放界面底图、招手动画帧和聆听动画帧。"],
            ["K1 至 K4 轻触按键", "唤醒、切换语言、亮度、音量。按下时接通地。"],
            ["KCD1-101 船型开关", "侧面总开关，串在充电板输出和升压模块之间的正电源上。"],
            ["TP4056 充电板", "用自己的 Type-C 给锂电池充电，输出经过开关再送给升压。"],
            ["MT3608 升压模块", "把电池电压升到大约 5.0 至 5.2 伏，直接供给开发板的 5 伏和功放。"],
            ["自画 PCB 载板", "把开发板、屏幕、摄像头、麦克风、功放、TF 卡和按键按确定的插座装到同一块板上。"],
            ["外壳", "把电路包起来。正面露出屏幕和摄像头，侧面留出开关，并留出声音和充电口。"],
        ],
        [52 * mm, 122 * mm],
    ))
    story.append(Spacer(1, 3 * mm))
    story.append(P("body", "供电顺序是：锂电池进到 TP4056，从充电板的输出经过侧面开关，进入 MT3608 升压，升压后的大约 5 伏接到开发板的 5 伏针和 MAX98357A。屏幕、TF 卡、麦克风和摄像头用的 3.3 伏，来自 ESP32-S3 开发板自己板上的稳压。所有地线在自画 PCB 上连成同一张地。"))

    story.append(P("h1", "自画 PCB 和外壳"))
    story.append(P("h2", "PCB 载板"))
    story.append(P("body", "小云宝的电路装在一块自己绘制的 PCB 上。这块板是载板：ESP32-S3 开发板、INMP441、屏幕排母、摄像头接口、功放和 TF 卡座都按实物位置落在板上，线不再用杜邦线临时跳。板子两面铺地，地上有成片过孔把顶层和底层连在一起。5 伏和 3.3 伏各自走电源网络。"))
    story.append(P("body", "屏幕从板子上的排母插进去，排线朝下。摄像头接口按 D0 到 D7、像素时钟、帧同步、行同步和两根配置线排好，主时钟不接 GPIO。麦克风插座按 INMP441 的两排三针放置，L/R 在板上接到地。四个按键接到 GPIO18、GPIO21、GPIO3 和 GPIO45，另一端进入地铜。开发板的 Type-C 仍留在开发板自己身上，用来供电和刷机；充电用的是 TP4056 模块上的口。"))
    story.append(P("h2", "外壳"))
    story.append(P("body", "外壳把这块 PCB 包成一台可以放在桌上的成品。正面开出屏幕窗口，云伴、气泡和动画从这里看；镜头孔对准 OV2640，让摄像头看到面前的人。侧面做出船型开关的开口，手可以从外面拨开和关。喇叭的位置留出出声的地方，充电口留在壳外可以插线。按键对准壳上的键帽或开孔，短按和长按都按得到。天线和开发板的 Type-C 不被壳壁堵死，合上外壳之后，外面能完成开关机、说话、看屏幕和充电。"))

    story.append(P("h1", "十五、线是怎么接的"))
    story.append(P("body", "麦克风和功放共用同一组数字音频时钟，数据脚分开。屏幕和 TF 卡共用同一组 SPI 时钟和数据，片选脚分开。摄像头用并行数据线、像素时钟和同步脚，配置总线单独走两根信号。四个按键都是一端接对应 GPIO，另一端接地，开发板内部把这些脚拉高，按下时变成低电平。"))
    story.append(table(
        ["信号", "GPIO", "接到"],
        [
            ["音频位时钟 BCLK", "4", "INMP441 的 SCK，MAX98357A 的 BCLK"],
            ["音频字时钟 WS", "5", "INMP441 的 WS，MAX98357A 的 LRC"],
            ["麦克风数据", "6", "INMP441 的 SD"],
            ["喇叭数据", "7", "MAX98357A 的 DIN"],
            ["屏幕复位 RST", "8", "ST7789 的 RST"],
            ["屏幕数据/命令 DC", "9", "ST7789 的 DC"],
            ["屏幕片选 CS", "10", "ST7789 的 CS"],
            ["SPI 数据 MOSI", "11", "屏幕 SDA，TF 卡 CMD"],
            ["SPI 时钟 SCK", "12", "屏幕 SCL，TF 卡 CLK"],
            ["SPI 数据 MISO", "13", "TF 卡 DO"],
            ["屏幕背光", "14", "ST7789 的 BL，由主控输出亮度"],
            ["TF 卡片选", "15", "TF 卡 CS"],
            ["唤醒键 K1", "18", "按键，另一端接地"],
            ["语言键 K2", "21", "按键，另一端接地"],
            ["亮度减 / 音量减 K3", "3", "按键，另一端接地"],
            ["亮度加 / 音量加 K4", "45", "按键，另一端接地"],
            ["摄像头数据 D0 至 D7", "16、17、38、39、40、41、42、43", "OV2640 数据线，按 D0 到 D7 的顺序"],
            ["摄像头像素时钟 PCLK", "44", "OV2640 的 PCLK"],
            ["摄像头帧同步 VSYNC", "47", "OV2640 的 VSYNC"],
            ["摄像头行同步 HREF", "46", "OV2640 的 HREF"],
            ["摄像头配置 SDA / SCL", "1 / 2", "OV2640 的 SCCB"],
            ["USB 数据", "19 / 20", "留在开发板自己的 Type-C 上"],
        ],
        [42 * mm, 58 * mm, 74 * mm],
    ))
    story.append(Spacer(1, 3 * mm))
    story.append(P("body", "INMP441 是两排三针。丝印朝上时，上排是 SCK、WS、L/R，下排是 SD、VDD、GND。L/R 接地，表示使用左声道。MAX98357A 从左到右是 LRC、BCLK、DIN、GAIN、SD、GND、VIN。GAIN 和 SD 保持模块出厂状态。屏幕排线按 1 脚标记对准：背光、片选、数据/命令、复位、数据、时钟、电源、地。摄像头的主时钟脚不接到 GPIO，因为这块 OV2640 模组自己带振荡器。"))

    story.append(P("h1", "十六、程序放在哪里，分成哪些文件"))
    story.append(P("body", "设备上的程序是一个 Arduino 工程，文件夹名叫 SmartCompanionS3。打开 SmartCompanionS3.ino 就能看到引脚、按键和主循环。其余功能按职责拆成头文件，由主程序在末尾依次收进来。中转服务是另一份 Python 程序，跑在云服务器上，文件是 relay-server/core/xiyunbao_relay.py。"))
    story.append(table(
        ["文件", "负责的事"],
        [
            ["SmartCompanionS3.ino", "引脚、按键、亮度音量、主循环。把屏幕、网络、语音和摄像头轮流调度起来。"],
            ["display_ui.h", "ST7789 初始化、180 度显示方向、底图、招手动画、聆听动画、离线提示和配网画面。"],
            ["chat_ui.h", "中文和英语的对话气泡、换行和滚动。"],
            ["audio_io.h", "INMP441 录音和 MAX98357A 播放。两者分时使用同一路 I2S。"],
            ["dialog_relay.h", "和中转服务器的 WebSocket。上传录音，接收识别文字和压缩语音。"],
            ["tts_pipeline.h", "把收到的语音放进独立任务播放，播完再把麦克风交回来。"],
            ["wifi_provision.h", "手机配网热点和 192.168.4.1 页面，并把新网络写进本机。"],
            ["wifi_wakeup.h", "读取已保存的 Wi-Fi，后台连接，以及没网时的页面提示。"],
            ["camera_emotion.h", "OV2640 出图、JPEG 上传，以及屏幕上的关怀句子。"],
            ["qwen_client.h", "情绪标记的整理，保证念出来和显示出来的是正文。"],
            ["secrets.local.h", "中转地址、端口、语音模型和音色。云端密钥留在服务器，不放进日常说明。"],
            ["xiyunbao_relay.py", "服务器中转。接收设备音频和照片，调用识别、对话和语音合成，再把结果送回设备。"],
        ],
        [48 * mm, 126 * mm],
    ))

    story.append(P("h1", "十七、整机是怎么连成一条链路的"))
    story.append(P("body", "小云宝自己不做大模型运算。它负责看、听、显示和播放。理解你说的话、组织回答、合成声音，都在服务器和云端完成。设备连上 Wi-Fi 之后，用一条 WebSocket 找到中转：47.95.194.77 的 8000 端口，路径是 /xiyunbao/v1/。连接所需的口令写在 secrets.local.h 里。"))
    story.append(P("h2", "你说话时"))
    story.append(P("step", "1. 短按 K1，主循环调用语音回合，麦克风开始采集。INMP441 以 16 kHz 送出音频。"))
    story.append(P("step", "2. dialog_relay.h 把这段音频经 WebSocket 发给中转服务器。"))
    story.append(P("step", "3. 中转把音频交给阿里云百炼的流式识别 qwen-audio-3.0-asr-flash-streaming。中文模式按中文听，英语模式同时接受英语和中文。"))
    story.append(P("step", "4. 你停下来之后，中转把听写结果交给千问 qwen-turbo。陪伴模式使用云朵伙伴的系统提示，英语模式使用口语陪练提示。"))
    story.append(P("step", "5. 千问一边写出回答，中转一边把可以开口的第一小句送给语音合成 qwen-audio-3.0-tts-flash，音色是 longtongxuxian。"))
    story.append(P("step", "6. 合成出的 24 kHz 声音在服务器上压成 Opus，再经同一条 WebSocket 回到设备。"))
    story.append(P("step", "7. 设备上的独立播放任务把 Opus 解开，经 MAX98357A 播出。chat_ui.h 同时把正文画进气泡。"))
    story.append(P("step", "8. 播放结束，音频从喇叭切回麦克风，下一句可以直接开始。"))
    story.append(P("h2", "摄像头时"))
    story.append(P("body", "camera_emotion.h 在后台任务里抓取 OV2640 的 JPEG。空闲时画面用来更新关怀句子；对话时以较低的频率把 JPEG 发给中转。中转做表情识别，把结果做成一条视觉线索交给千问，用来选择语气。屏幕上的关怀和嘴里说的正文因此对着同一次表情。"))
    story.append(P("h2", "配网时"))
    story.append(P("body", "长按 K1 后，wifi_provision.h 让开发板自己变成热点 XiaoYunBao- 加四位号码。手机连上后访问 192.168.4.1，页面由设备上的小型 Web 服务提供。你提交的网络名称和密码写入本机存储。下次开机，wifi_wakeup.h 用这份记录直接去连接。"))

    story.append(P("h1", "十八、主循环里谁先谁后"))
    story.append(P("body", "上电后，程序先点亮屏幕、挂上 TF 卡、读出已保存的 Wi-Fi，再把招手动画放进 PSRAM，画出陪伴主页。进入主循环以后，每一圈都做这些事：驱动与中转的连接，扫描四枚按键，处理亮度、音量和语言切换，推动招手或聆听动画，在该听的时候读取麦克风，在该说的时候把语音交给播放任务，并按摄像头的节奏抓一张画面。"))
    story.append(P("body", "听和说不会同时占着音频口。要听的时候，I2S 配成麦克风输入；要说的时候，同一路 I2S 配成功放输出。切语言或打断时，程序先让播放任务停下来，再把音频口交还给麦克风，然后开始新的一轮。这样按键、动画和网络可以继续走，不会互相盖住。"))

    story.append(Spacer(1, 8 * mm))
    story.append(P("body", "小云宝就这样放在你的桌上：开关打开，它在；你看着它，它在屏幕上陪你；你按一下，它听你说，并把回答说给你听。中文时它是云朵伙伴，英语时它是口语陪练。屏幕、摄像头、麦克风、喇叭和四个按键留在你面前，识别、对话和声音合成通过中转连到云端。同一台机器，两种相处方式，都从侧面那枚开关和面前这块屏幕开始。"))

    doc = SimpleDocTemplate(
        str(OUTPUT),
        pagesize=A4,
        leftMargin=18 * mm,
        rightMargin=18 * mm,
        topMargin=18 * mm,
        bottomMargin=16 * mm,
        title="小云宝完整使用手册",
        author="小云宝",
    )
    doc.build(story, onFirstPage=header_footer, onLaterPages=header_footer)
    print(OUTPUT)


if __name__ == "__main__":
    register_fonts()
    S = make_styles()
    build()
