# Тестовый хук почвенного материала (opaque-вариант): вместо запечённой
# `.tex` карты кладёт на землю тестовый паттерн `mm_bkg01.bmp` и пишет зонды в
# hdlog.txt (подтверждённый канал наблюдения, `docs/KNOWLEDGE.md`).
# Структура модуля повторяет родной генерируемый материал (проверено dis'ом
# `data/materials/bald_hill_opaque_0.pyc`).
# Ставится ТОЛЬКО в scratch-дерево: python3 tools/tex/hook_ground.py --tree <дерево>
import globalscript

Passability = globalscript.GROUND
AcceptCamera = 1


def _log(s):
    f = open('hdlog.txt', 'a')
    f.write('TEXHOOK ' + s + '\n')
    f.flush()
    f.close()


_log('opaque module ' + __name__)


def InitMaterial(Mtl):
    _log('opaque init ' + repr(Mtl))
    p = Mtl.GetPass(0)
    p.AddTexture('mm_bkg01.bmp')
    n = p.GetNumberOfTextures()
    t = p.GetTexture(0)
    _log('pass=' + repr(p) + ' n=' + repr(n) + ' tex=' + repr(t) +
         ' w=' + repr(t.GetWidth()) + ' h=' + repr(t.GetHeight()))
