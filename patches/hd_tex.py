# runtime-зонд: чек-поинты по шагам, поиск безопасного способа дампа пикселей
import gamemodule, globalscript


def Probe():
    log = open('hdlog.txt', 'a')
    log.write('HDSTEP 0\n')
    log.flush()
    mat = gamemodule.LoadMaterial('mm_bkg01.bmp')
    log.write('HDSTEP 1 mat=' + repr(mat) + '\n')
    log.flush()
    p = mat.GetPass(0)
    log.write('HDSTEP 2 pass=' + repr(p) + '\n')
    log.flush()
    p.AddTexture('mm_bkg01.bmp')
    n = p.GetNumberOfTextures()
    log.write('HDSTEP 3 n=' + repr(n) + '\n')
    log.flush()
    t = p.GetTexture(0)
    log.write('HDSTEP 4 t=' + repr(t) + '\n')
    log.flush()
    w = t.GetWidth()
    h = t.GetHeight()
    log.write('HDSTEP 5 size=' + repr((w, h)) + '\n')
    log.flush()
    import pickle
    s = pickle.dumps(t)
    log.write('HDSTEP 6 pickle len=' + repr(len(s)) + '\n')
    log.flush()
    log.close()
