# Тестовый хук почвенного материала (alpha-вариант): тестовый паттерн +
# blend-функция, как в родном генерируемом материале (проверено dis'ом
# `data/materials/bald_hill_alpha_0.pyc`). Зонды в hdlog.txt.
# Ставится ТОЛЬКО в scratch-дерево: python3 tools/tex/hook_ground.py --tree <дерево>
import globalscript

Passability = globalscript.GROUND
AcceptCamera = 1


def _log(s):
    f = open('hdlog.txt', 'a')
    f.write('TEXHOOK ' + s + '\n')
    f.flush()
    f.close()


_log('alpha module ' + __name__)


def InitMaterial(Mtl):
    _log('alpha init ' + repr(Mtl))
    p = Mtl.GetPass(0)
    p.AddTexture('mm_bkg01.bmp')
    p.SetBlendFunc(globalscript.BBlend_Blend)
    n = p.GetNumberOfTextures()
    t = p.GetTexture(0)
    _log('pass=' + repr(p) + ' n=' + repr(n) + ' tex=' + repr(t) +
         ' w=' + repr(t.GetWidth()) + ' h=' + repr(t.GetHeight()))
