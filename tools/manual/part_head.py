import json, os, html, datetime
R='/home/claude/manual/root/project'
B={'automap':'9f9c006e6022a94805952c2042b4021e','charsel':'0f8d56daf5816a5ff73a2880c76b9422','cruel01':'bb5b417221e98d9f72e1cb470f2afb2c',
'ctfbase':'88e707fd4287143ac77551f30fc6dc0c','ctfflag':'8904006b956ef7f1ff0920793cf0a840','ctfpick':'b3f49d2afc38abcfb62f1a06a08f0a78',
'logo':'80b774254cc6140efe3f7acf622b4cfa','link':'095ea1c570d0297f2a2fb5ede1fb5128','match':'149cac1a3dd8a5e18fdf1ce5af330839',
'camp':'8c4dbc6c244de4a6e9b875bcdfca8d2f','hub':'18e90f86978472c2000d2f56942ededb','quick':'ccec682033fb3e95be10ee0a42c9a372',
'title':'2c7fa5e11d80350b1519cc5d2167e583','pause':'f9d5ea6ff72391e473160857ef5ab3e8','chad':'a7b2f3ba659853b88897d42d10540e8a',
'stacy':'78d6e6e7b5cb01f78d34264805659e73','hud':'f01018717e7afbea4c91aa0113b77816','space':'ab63eb76bce7283e584737aaea5382ad','xeno':'0c43f6b81cc6c8716e16888cd1f17593'}
def U(k): return '/_blob/'+B[k]
P=454            # 120 mm booklet page
INK='#17161A'; PAPER='#F5F2EA'; GOLD='#8A5A12'; GOLDL='#E3A94B'; DARK='#121216'; MUTED='#55524C'; RED='#B3261E'; BLUE='#1F5FA8'
FONTS='<link rel="preconnect" href="https://fonts.googleapis.com"><link href="https://fonts.googleapis.com/css2?family=Chakra+Petch:wght@500;600;700&amp;family=Inter:wght@400;500;600;700&amp;display=swap" rel="stylesheet">'
def page_file(name, title, w, h, inner, bg=PAPER):
    return f'''<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>{html.escape(title)}</title>
<script src="./support.js"></script>
</head>
<body>
<x-dc>
<helmet>
{FONTS}
<style>
body{{margin:0}}
a{{color:{GOLD}}}a:hover{{color:{INK}}}
</style>
</helmet>
<div style="width: {w}px; height: {h}px; box-sizing: border-box; position: relative; overflow: hidden; background: {bg}; font-family: Inter, system-ui, sans-serif; color: {INK}">
{inner}
</div>
</x-dc>
<script type="text/x-dc" data-dc-script data-props='{{"$preview":{{"width":{w},"height":{h}}}}}'>
class Component extends DCLogic {{
renderVals() {{
return {{}};
}}
}}
</script>
</body>
</html>
'''
