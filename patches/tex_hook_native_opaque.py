# Зонд-хук почвенного opaque-материала: грузит **родную** `.tex` карты, имя которой
# выводится из имени модуля (`<map>_opaque_N` -> `<map>_N.tex`), и пишет в hdlog.txt
# размер текстуры ДО и ПОСЛЕ `AddTexture`. Это разделяет «движок падает внутри
# загрузки .tex» (есть строка PRE, нет POST) и «материал загружен, падают позже»
# (есть POST). Структура повторяет родной генерируемый материал (проверено dis'ом
# `data/materials/bald_hill_opaque_0.pyc`).
# Ставится ТОЛЬКО в scratch-дерево: python3 tools/tex/hook_ground.py --tree <дерево> --native
import globalscript

Passability = globalscript.GROUND
AcceptCamera = 1


def _log(s):
    f = open('hdlog.txt', 'a')
    f.write('TEXHOOK ' + s + '\n')
    f.flush()
    f.close()


def _texname(mod):
    # 'portal_domain_opaque_1' -> 'portal_domain_1.tex'
    if '_opaque_' in mod:
        head, num = mod.split('_opaque_')
    elif '_alpha_' in mod:
        head, num = mod.split('_alpha_')
    else:
        head, num = mod, '0'
    return head + '_' + num + '.tex'


_name = _texname(__name__)
_log('native-opaque module ' + __name__ + ' tex ' + _name)


def InitMaterial(Mtl):
    p = Mtl.GetPass(0)
    n0 = p.GetNumberOfTextures()
    _log('PRE name=' + _name + ' pass=' + repr(p) + ' n=' + repr(n0))
    p.AddTexture(_name)
    n1 = p.GetNumberOfTextures()
    t = p.GetTexture(0)
    _log('POST name=' + _name + ' n=' + repr(n1) +
         ' w=' + repr(t.GetWidth()) + ' h=' + repr(t.GetHeight()))
