/* ============================================================
 * NX 远程控制器 - 相册(缩略图网格 + Lightbox)
 * 从相机端 /DCIM 目录抓取文件列表(递归子目录),
 * 渲染为缩略图网格;点击缩略图打开 Lightbox 自适应缩放查看。
 *
 * 内存/带宽优化(2026-08):
 * 1. 缩略图不再直接引用原图。优先走相机端缩略图 CGI
 *    (ImageMagick convert 生成,见 nx-rc/thumb/),单张 4-8MB -> 15-30KB;
 *    CGI 不可用时自动回退原图(卡但可用)。
 * 2. 分批渲染:每次只建一批 DOM(30 张),滚动到底自动加载下一批,
 *    避免几百个缩略图一次性进 DOM 导致内存暴涨。
 * 3. IntersectionObserver 按视口加载/释放:滚入视口才设置 src,
 *    滚出视口即清空 src 释放解码内存;不支持 IO 的浏览器退化为
 *    loading="lazy" 原逻辑。
 * ============================================================ */

/* 支持的媒体扩展名 */
Gallery.MEDIA_EXTS = {
    '.jpg': 'image', '.jpeg': 'image', '.png': 'image', '.gif': 'image',
    '.bmp': 'image', '.tif': 'image', '.tiff': 'image',
    '.mov': 'video', '.mp4': 'video', '.avi': 'video',
    '.3gp': 'video', '.m4v': 'video', '.webm': 'video'
};

/* 缩略图服务配置(相机端 thumb CGI,端口与 capdtm httpd 一致)
 *
 * 双尺寸(2026-09-06):
 *   grid = 网格缩略图,手机 3 列约 130px/格,320px 在 2x 屏上足够清晰,
 *          单张约 25-30KB;预热脚本会提前铺满,命中缓存 0.2s 出图。
 *   full = 灯箱全屏查看,1024px/q85 约 150-250KB,比直接拉 4-8MB 原图
 *          快一个数量级;需要打印/细看时再点"原图"按钮单独加载。
 */
Gallery.THUMB = {
    enabled: true,   // 相机端已部署 thumb CGI 时保持 true;未部署可置 false 退回原图
    port: 8080,      // busybox httpd 端口(与 capdtm-httpd.sh 一致)
    grid: {
        width: 320,  // 网格缩略图长边(与 thumb-prewarm.sh 的第一组参数一致)
        quality: 75
    },
    full: {
        width: 1024, // 灯箱大图长边(与 thumb-prewarm.sh 的第二组参数一致)
        quality: 85
    }
};

/* 网格缩略图加载失败后的重试次数(带 cache-buster)。
 * 相机端冷生成偶发返回空 body,重试一次即可命中刚写好的缓存。
 * 超过次数才回退原图 —— 原图 4-8MB,手机上代价远大于一次重试。 */
Gallery.THUMB_RETRY = 1;

/* 同时发起的缩略图请求上限。
 *
 * 相机端 busybox httpd 是串行 fork ImageMagick 的,单核 CPU 下一次
 * convert 就要占满一个核。前端一次丢 30 个请求过去并不会并行,只会:
 *   1) 请求在 httpd 队列里堆积,首屏等待时间 = 所有 convert 串行耗时之和;
 *   2) CPU 长期 100%,80 端口 daemon 的 /api/v1/camera/status 心跳被饿死
 *      超时,前端误判"相机已断开"(点掉弹窗后图片其实照常加载)。
 * 因此前端主动限流,让相机端一次只处理少量请求,心跳才有空隙响应。
 */
Gallery.MAX_CONCURRENT = 2;

/* 目录树抓取:并发上限(用户连续点开多个目录时,同时只发这么多目录请求)。
 * 目录请求本身轻量(只取目录 HTML),不像缩略图 convert 那样吃满 CPU,
 * 但也要节制,避免把 daemon 的 HTTP 连接池占满拖慢遥控心跳。
 */
Gallery.DIR_CONCURRENCY = 3;

/* 目录清单内存缓存的保鲜时间(ms)。
 * 切走相册再切回来时,30 秒内不重新抓目录树,直接用缓存重建视图(秒开)。
 * 之前是 2 分钟,导致"遥控页拍完照切回相册看不到新照片"——
 * 拍照到回看的间隔通常落在 2 分钟内,全部被缓存吞掉;收紧到 30 秒
 * 既能挡住快速来回切页签的重复抓取,又不影响新照片回看(2026-09-04)。
 */
Gallery.TREE_CACHE_TTL = 30000;

/* 默认展开的"最新"目录数(其余目录收起,点目录头才加载) */
Gallery.DEFAULT_EXPAND = 1;

function Gallery(controllers) {
    this.controllers = controllers;
    this.iframe = null;
    this.rootUrl = null;     // 图库根地址,如 http://ip/DCIM
    this.groups = [];        // 目录清单 [{name,url,loaded,loading,items:[{file,index}]}]
    this.media = [];         // 已加载媒体的展平列表(顺序即显示顺序)
    this.rootFailed = false; // 根目录抓取是否失败
    this.lightbox = null;    // Lightbox jQuery 对象
    this.current = -1;       // Lightbox 当前索引
    this.touchStartX = 0;    // 触摸滑动起始 X
    this.selectMode = false; // 多选模式
    this.selection = {};     // 已选中项的索引集合
    this.downloading = false;// 批量下载进行中
    this._obs = null;        // IntersectionObserver(图片)
    this._loadQueue = [];    // 待发起的缩略图请求队列(限流)
    /* 在飞请求用「票据表」而不是计数器：token -> img。
     * 2026-11-22：计数器只靠 load/error 回落，而"取消"（滚出视口、收起目录、
     * 重进相册）会 removeAttribute('src') 让浏览器丢弃请求 —— 多数情况下不派发
     * 事件，于是计数永久泄漏；一旦泄漏到 MAX_CONCURRENT，整个网格再也不发请求。
     * 票据表让每次请求都能被主动结算，且结算幂等。 */
    this._inflight = {};     // token -> img（在飞）
    this._inflightCount = 0; // 在飞数量（= 键数，避免每次 Object.keys）
    this._thumbSeq = 0;      // 票据递增号
    this._gen = 0;           // 相册代际：init/切相机时 +1，旧回调据此丢弃
    this._loading = 0;       // 兼容旧字段，仅镜像 _inflightCount
    this._dirLoading = 0;    // 正在抓取中的目录数(限制目录请求并发)
    this._treeCache = null;  // 目录树缓存 {rootUrl, ts, groups:[...]}
    this._builtAt = 0;       // 上次成功构建视图的时间
    this._ioSupported = ('IntersectionObserver' in window) &&
                        ('IntersectionObserverEntry' in window);
}

/* ============ 初始化 ============ */

/* opts.force=true 强制重新抓取(刷新按钮);否则命中 2 分钟缓存直接重建视图 */
Gallery.prototype.init = function (url, opts) {
    var self = this;

    if (typeof url === 'undefined' || url === null || url === '') {
        url = 'http://' + app.hostname + '/DCIM';
    }
    self.rootUrl = url;
    /* 代际号：切相机、挂载缓存、强制刷新都会走到这里。晚到的目录回调据此丢弃，
     * 否则旧相机的目录内容会被追加到已经重建的 DOM 里。 */
    self._gen++;
    var force = !!(opts && opts.force);

    // 缓存命中:直接用已抓到的目录/文件数据重建视图(免一次整树抓取)
    if (!force && self._treeCache &&
        self._treeCache.rootUrl === url &&
        (Date.now() - self._treeCache.ts) < Gallery.TREE_CACHE_TTL) {
        self.rootFailed = false;
        self.groups = self._treeCache.groups;
        self.media = self._treeCache.media;
        self.resetThumbQueue();
        self.mountFromCache();
        return;
    }

    self._treeCache = null;
    self.groups = [];
    self.media = [];
    self.rootFailed = false;
    self._dirLoading = 0;
    self.resetThumbQueue();

    // 先搭骨架再抓根目录:一旦读到目录清单即可立即渲染目录头。
    self.startRender();
    self.fetchRootTree(url);
};

/* ============ 目录树加载(只抓根清单,目录内容按需点开) ============ */

/* 抓根目录页:得到子目录清单后按名倒序(最新在上)渲染目录头,
 * 默认展开最新的 DEFAULT_EXPAND 个目录,其余收起。点开才抓该目录的文件。
 * 不再递归全量抓取 —— 这是"目录多时相册慢"的根治点。 */
Gallery.prototype.fetchRootTree = function (rootUrl) {
    var self = this;
    var gen = self._gen;
    self.fetchDir(rootUrl).then(function (res) {
        if (self._gen !== gen) { return; }   // 期间切过相机/刷新过：结果作废
        var $g = $('#gallery');
        $g.find('.gallery-loading').remove();

        var names = [];
        var i;
        for (i = 0; i < res.dirs.length; i++) {
            var d = res.dirs[i];
            var nm = d.replace(/\/$/, '').split('/').pop();
            if (nm && nm !== '根目录') {
                names.push(nm);
            }
        }
        // 倒序:数字目录(100PHOTO..299PHOTO)最新在上;字母目录按字典序倒排
        names.sort().reverse();

        if (names.length === 0) {
            // 根目录无子目录:把根目录自身当做一个目录组(容错)
            self.groups = [{
                name: rootUrl.replace(/\/$/, '').split('/').pop() || 'DCIM',
                url: rootUrl,
                loaded: false, loading: false, items: []
            }];
            self.renderDirSections();
            self.toggleDir(self.groups[0].name);
            self.finishTreeLoad();
            return;
        }

        var j;
        for (j = 0; j < names.length; j++) {
            self.groups.push({
                name: names[j],
                url: rootUrl + (rootUrl.charAt(rootUrl.length - 1) === '/' ? '' : '/') +
                     names[j] + '/',
                loaded: false, loading: false, items: []
            });
        }
        self.renderDirSections();
        self.finishTreeLoad();

        // 默认展开最新目录(用户最想看刚拍的照片)
        var toExpand = Math.min(Gallery.DEFAULT_EXPAND, self.groups.length);
        for (j = 0; j < toExpand; j++) {
            self.toggleDir(self.groups[j].name, true);
        }
    }, function () {
        if (self._gen !== gen) { return; }
        self.rootFailed = true;
        var $g = $('#gallery');
        $g.find('.gallery-loading').remove();
        $g.find('.gallery-count').text('读取失败');
        $g.append('<div class="gallery-error">无法读取相机相册目录(' +
                  rootUrl + ')。<br>请确认相机 Wi-Fi 已开启、Web 服务可用。</div>');
    });
};

/* 根清单读取完毕:缓存整棵树(供切页签回来时秒开) */
Gallery.prototype.finishTreeLoad = function () {
    var self = this;
    var $g = $('#gallery');
    var totalDirs = self.groups.length;
    var loadedFiles = self.media.length;
    $g.find('.gallery-count').text(totalDirs + ' 个目录 · ' +
                                  loadedFiles + ' 张(已展开)');
    self._treeCache = {
        rootUrl: self.rootUrl,
        ts: Date.now(),
        groups: self.groups,
        media: self.media
    };
    self._builtAt = Date.now();
};

/* 渲染目录头列表(全部折叠态),供点击展开 */
Gallery.prototype.renderDirSections = function () {
    var self = this;
    var $g = $('#gallery');
    var i;
    for (i = 0; i < self.groups.length; i++) {
        var g = self.groups[i];
        var $sec = $('<div class="gallery-section" data-dir="' +
                     self.escAttr(g.name) + '"></div>');
        var $head = $('<div class="gallery-dir-head" title="点击展开/收起"></div>');
        $head.append('<span class="gallery-dir-arrow">&#9654;</span>');
        $head.append($('<span class="gallery-dir-name"></span>').text(g.name));
        $head.append('<span class="gallery-dir-meta"></span>');
        var $grid = $('<div class="gallery-grid"></div>').hide();
        $sec.append($head).append($grid);
        $g.append($sec);

        (function (grp, sec) {
            $head.on('click', function (ev) {
                ev.stopPropagation();
                if (grp.loading) {
                    return; // 加载中不可重复点
                }
                self.toggleDir(grp.name);
            });
        })(g, $sec);
    }
    if (self.groups.length === 0) {
        $g.append('<div class="gallery-empty">SD 卡上没有找到照片目录。</div>');
    }
};

Gallery.prototype.escAttr = function (s) {
    return String(s).replace(/&/g, '&amp;').replace(/"/g, '&quot;')
                    .replace(/</g, '&lt;').replace(/>/g, '&gt;');
};

/* 找目录 */
Gallery.prototype.findGroup = function (name) {
    var self = this;
    var i;
    for (i = 0; i < self.groups.length; i++) {
        if (self.groups[i].name === name) {
            return self.groups[i];
        }
    }
    return null;
};

/* 展开/收起目录;未加载的目录先抓文件再展开 */
Gallery.prototype.toggleDir = function (name, forceExpand) {
    var self = this;
    var g = self.findGroup(name);
    if (!g) {
        return;
    }
    var $sec = $('.gallery-section[data-dir="' + self.escAttr(name) + '"]');
    if ($sec.length === 0) {
        return;
    }

    if (!g.loaded && !g.loading) {
        self.loadDir(g, $sec);
        return;
    }
    if (g.loading) {
        return;
    }

    var willExpand = forceExpand === true || !g.expanded;
    g.expanded = willExpand;
    var $grid = $sec.find('.gallery-grid');
    if (willExpand) {
        $sec.find('.gallery-dir-arrow').html('&#9660;');
        $grid.show();
    } else {
        // 收起:释放网格内缩略图,节省解码内存
        // 同时取消还在排队的目录请求 —— 否则收起后它仍会占一个并发位，
        // 回来时把结果写进已经隐藏的网格（用户看到"点开没内容，等一下又冒出来"）。
        if (g._pollId) { clearInterval(g._pollId); g._pollId = null; }
        g.loading = false;
        $sec.find('.gallery-dir-arrow').html('&#9654;');
        self.releaseGridThumbs($grid);
        $grid.hide();
    }
    self.refreshCounter();
};

/* 抓取目录文件列表并渲染网格 */
Gallery.prototype.loadDir = function (g, $sec) {
    var self = this;
    var gen = self._gen;
    g.loading = true;
    $sec.find('.gallery-dir-arrow').html('&#9660;');
    $sec.find('.gallery-dir-meta').html(
        '<span class="gallery-dir-spinner"></span> 读取中');
    $sec.find('.gallery-grid').show();

    /* 限流：超过 DIR_CONCURRENCY 时排队等前面的完成。
     * 句柄挂在目录组上（g._pollId），收起目录/重进相册/切相机时可以真正取消。
     * 原来这里是局部的 `var timer`，从未赋值，`clearTimeout(timer)` 是死代码 ——
     * 排队中的目录在界面已经被拆掉之后仍会发出请求，并把结果写进新的 DOM。 */
    var poll = setInterval(function () {
        if (self._gen !== gen) { clearInterval(poll); g._pollId = null; return; }
        if (self._dirLoading < Gallery.DIR_CONCURRENCY) {
            clearInterval(poll);
            g._pollId = null;
            self._dirLoading++;
            self.fetchDir(g.url).then(function (res) {
                /* ★ 2026-11-22 P1：归属必须先判断，再改状态。
                 * 切走/重进时 startRender 会把 _dirLoading 归零，而 groups 是
                 * _treeCache.groups 的对象引用（同一个 g 被复用）。
                 * 旧请求若先减计数，扣掉的是新请求刚占的名额（Math.max 只防负数，
                 * 防不了并发超限）；若先清 g.loading，会把新请求的"加载中"抹掉，
                 * 点击被重新放开 ⇒ 同一目录被重复请求。
                 * 本代已被重置，旧请求的槽位也随之作废，它不该再碰任何状态。 */
                if (self._gen !== gen) { return; }
                self._dirLoading = Math.max(0, self._dirLoading - 1);
                g.loading = false;
                self.buildDirGrid(g, res.files);
            }, function () {
                if (self._gen !== gen) { return; }
                self._dirLoading = Math.max(0, self._dirLoading - 1);
                g.loading = false;
                $sec.find('.gallery-dir-meta').text('读取失败(点击重试)');
                $sec.find('.gallery-dir-arrow').html('&#9654;');
            });
        }
    }, 80);
    g._pollId = poll;
};

/* 把抓到的文件列表构建为网格(文件最新在前) */
Gallery.prototype.buildDirGrid = function (g, rawFiles) {
    var self = this;
    var $sec = $('.gallery-section[data-dir="' + self.escAttr(g.name) + '"]');
    var $grid = $sec.find('.gallery-grid');

    // 过滤媒体,并倒序 -> 目录内最新照片在前
    var files = [];
    var i;
    for (i = 0; i < rawFiles.length; i++) {
        var f = rawFiles[i];
        var type = Gallery.MEDIA_EXTS[f.ext];
        if (type) {
            files.push({url: f.url, name: f.name, type: type, dir: g.name});
        }
    }
    files.reverse();

    var items = [];
    for (i = 0; i < files.length; i++) {
        var idx = self.media.length;
        self.media.push(files[i]);
        items.push({file: files[i], index: idx});
    }
    g.items = items;
    g.loaded = true;
    g.expanded = true;

    if (items.length === 0) {
        $grid.empty();
        $grid.append('<div class="gallery-dir-empty">此目录没有照片/视频</div>');
    } else {
        var frag = document.createDocumentFragment();
        for (i = 0; i < items.length; i++) {
            frag.appendChild(self.buildItem(items[i])[0]);
        }
        $grid.empty().append(frag);
    }
    $sec.find('.gallery-dir-meta').text(items.length + ' 项');
    self.refreshCounter();
};

/* 收起目录时释放其缩略图 src(解码内存)。
   仅在支持 IntersectionObserver 时清 src —— 不支持 IO 的降级浏览器
   依赖 img 自带 src + loading=lazy,清掉后重展开将无法恢复。 */
Gallery.prototype.releaseGridThumbs = function ($grid) {
    var self = this;
    if (!self._ioSupported) {
        return;
    }
    var imgs = $grid.find('img').get();
    var i;
    for (i = 0; i < imgs.length; i++) {
        var el = imgs[i];
        // 排队中/在飞的请求在这里主动结算，不依赖浏览器的 load/error
        self.cancelThumb(el);
        el._state = 'idle';
        el._loaded = false;
        el._cancelled = true;
        if (el.getAttribute('src')) {
            el.removeAttribute('src');
        }
    }
};

/* 顶部计数 */
Gallery.prototype.refreshCounter = function () {
    var self = this;
    var loaded = 0;
    var i;
    for (i = 0; i < self.groups.length; i++) {
        if (self.groups[i].loaded) {
            loaded += self.groups[i].items.length;
        }
    }
    $('#gallery').find('.gallery-count').text(
        self.groups.length + ' 个目录 · ' + loaded + ' 张(已展开)');
};

/* 命中缓存:不重抓目录树,直接按已存 groups 重建视图 */
Gallery.prototype.mountFromCache = function () {
    var self = this;
    self.startRender();
    self.renderDirSections();
    var i;
    for (i = 0; i < self.groups.length; i++) {
        var g = self.groups[i];
        if (!g.loaded) {
            /* ★ 2026-11-22 P1：缓存里可能有「加载到一半」的目录。
             * 切出再切回时旧的 poll 只会因代际不符而 clearInterval + 清 _pollId，
             * **不会清 g.loading**；而本函数又 continue 跳过它。
             * 结果 g.loading 永远为 true，点击在 toggleDir 里被 `if (g.loading) return` 拦住，
             * 这个目录从此打不开（实测：loading=true、poll=null）。
             * 缓存只保留已完成的数据；未完成的目录把请求态重置，等用户再点时重新加载。 */
            if (g._pollId) { clearInterval(g._pollId); g._pollId = null; }
            g.loading = false;
            continue;
        }
        var $sec = $('.gallery-section[data-dir="' + self.escAttr(g.name) + '"]');
        var $grid = $sec.find('.gallery-grid');
        g.expanded = true;
        $sec.find('.gallery-dir-arrow').html('&#9660;');
        $sec.find('.gallery-dir-meta').text(g.items.length + ' 项');
        var frag = document.createDocumentFragment();
        var j;
        for (j = 0; j < g.items.length; j++) {
            frag.appendChild(self.buildItem(g.items[j])[0]);
        }
        $grid.empty().append(frag);
        $grid.show();
    }
    self.refreshCounter();
    self._builtAt = Date.now();
};

/* 目录列表服务:8080 实时 dirlist CGI 优先(2026-09-05)。
 * 80 端口 daemon 的 /DCIM 目录页是启动快照,新拍照片不出现,
 * 导致相册看不到最新照片;8080 CGI 每次请求都实时 readdir SD 卡。
 * dirlist 不可用时回退 80 端口 HTML 解析(旧行为)。 */
Gallery.DIRLIST = {
    enabled: true,
    port: 8080
};

/* 抓取单个目录:优先 JSON 实时列表,失败回退 HTML 解析 */
Gallery.prototype.fetchDir = function (url) {
    var self = this;
    return self.fetchDirJson(url).then(null, function () {
        return self.fetchDirHtml(url);
    });
};

/* 从 80 端口目录 URL 提取 DCIM 相对路径('' = 根) */
Gallery.dirRelPath = function (url) {
    var m = String(url).match(/\/DCIM\/?(.*)$/);
    if (!m) {
        return null;
    }
    return decodeURIComponent(m[1]).replace(/\/+$/, '');
};

/* 8080 dirlist:实时 JSON 目录列表 */
Gallery.prototype.fetchDirJson = function (url) {
    var self = this;
    return new Promise(function (resolve, reject) {
        if (!Gallery.DIRLIST.enabled) {
            reject(new Error('dirlist disabled'));
            return;
        }
        var rel = Gallery.dirRelPath(url);
        if (rel === null) {
            reject(new Error('not a DCIM url: ' + url));
            return;
        }
        /* host 必须从被请求的 url 推导，不能用 app.hostname。
         * app.hostname 是页面加载时的值；切相机时 app.js 只改 gallery.rootUrl，
         * 于是目录清单一直打旧相机，而缩略图 URL 由 file.url 的 host 生成 ——
         * 清单来自 A 相机、图片来自 B 相机，两边的文件名对不上。 */
        var host = '';
        try {
            host = new URL(url).hostname;
        } catch (e) {
            host = app.hostname || '';
        }
        if (!host) {
            reject(new Error('cannot resolve host for ' + url));
            return;
        }
        $.ajax({
            url: 'http://' + host + ':' + Gallery.DIRLIST.port +
                 '/cgi-bin/dirlist?p=' + encodeURIComponent(rel),
            dataType: 'json',
            cache: false,
            timeout: 15000
        }).done(function (res) {
            if (!res || res.ok !== true) {
                reject(new Error('dirlist bad response'));
                return;
            }
            var baseUrl = 'http://' + host + '/DCIM/' + (rel ? rel + '/' : '');
            var out = {dirs: [], files: []};
            var i;
            for (i = 0; i < (res.dirs || []).length; i++) {
                out.dirs.push(baseUrl + res.dirs[i] + '/');
            }
            for (i = 0; i < (res.files || []).length; i++) {
                var name = res.files[i];
                var m = name.match(/\.([a-zA-Z0-9]+)$/);
                var ext = m ? '.' + m[1].toLowerCase() : '';
                if (ext && Gallery.MEDIA_EXTS[ext]) {
                    out.files.push({url: baseUrl + name, name: name, ext: ext});
                }
            }
            resolve(out);
        }).fail(function () {
            reject(new Error('dirlist fetch failed'));
        });
    });
};

/* 80 端口回退:抓取目录页 HTML(Mongoose "Index of" 表格) */
Gallery.prototype.fetchDirHtml = function (url) {
    return new Promise(function (resolve, reject) {
        $.ajax({
            url: url,
            dataType: 'text',
            cache: false,
            timeout: 20000
        }).done(function (html) {
            resolve(Gallery.parseDirHtml(html, url));
        }).fail(function () {
            reject(new Error('fetch failed: ' + url));
        });
    });
};

/* 解析目录页 HTML:提取子目录与媒体文件链接 */
Gallery.parseDirHtml = function (html, baseUrl) {
    var result = {dirs: [], files: []};
    var doc;
    try {
        doc = new DOMParser().parseFromString(html, 'text/html');
    } catch (e) {
        return result;
    }
    var links = doc.querySelectorAll('a[href]');
    var i;
    for (i = 0; i < links.length; i++) {
        var a = links[i];
        var href = a.getAttribute('href');
        if (!href || href.charAt(0) === '#' || href.indexOf('javascript:') === 0) {
            continue;
        }
        var text = a.textContent || '';
        var isDir = href.charAt(href.length - 1) === '/' ||
                    text.indexOf('[DIRECTORY]') !== -1;
        var abs = Gallery.resolveUrl(baseUrl, href);
        if (!abs) {
            continue;
        }
        if (isDir) {
            result.dirs.push(abs);
        } else {
            var m = text.match(/\.([a-zA-Z0-9]+)$/);
            var ext = m ? '.' + m[1].toLowerCase() : '';
            if (ext && Gallery.MEDIA_EXTS[ext]) {
                result.files.push({url: abs, name: text, ext: ext});
            }
        }
    }
    return result;
};

/* 相对链接解析为绝对地址 */
Gallery.resolveUrl = function (baseUrl, href) {
    if (href.indexOf('http://') === 0 || href.indexOf('https://') === 0) {
        return href;
    }
    // 目录页 URL 视作目录:解析相对链接前补尾斜杠,
    // 否则 new URL('100PHOTO/', 'http://host/DCIM') 会解析到 /100PHOTO/
    var base = baseUrl;
    var pathPart = base.split(/[?#]/)[0];
    var lastSeg = pathPart.substring(pathPart.lastIndexOf('/') + 1);
    var looksLikeFile = lastSeg.indexOf('.') !== -1;
    if (!looksLikeFile && base.charAt(base.length - 1) !== '/') {
        base += '/';
    }
    try {
        return new URL(href, base).href;
    } catch (e) {
        var idx = base.indexOf('?');
        var b = idx >= 0 ? base.substring(0, idx) : base;
        if (href.charAt(0) === '/') {
            var m = b.match(/^(https?:\/\/[^\/]+)/);
            return m ? m[1] + href : null;
        }
        if (b.charAt(b.length - 1) !== '/') {
            b += '/';
        }
        return b + href;
    }
};

/* ============ 渲染缩略图网格 ============ */

/* 搭骨架:工具条 + loading 提示 + 观察器。
   在抓根目录前调用,读到目录清单后即可立即渲染目录头。 */
Gallery.prototype.startRender = function () {
    var self = this;
    var $g = $('#gallery').empty();
    self.selection = {};
    self.selectMode = false;

    $g.append(self.buildToolbar());
    $g.find('.gallery-count').text('正在读取...');
    $g.append('<div class="gallery-loading"><span class="gallery-spinner"></span>' +
              '正在读取相机相册,请稍候...</div>');

    self.initObserver();
    self.initLightbox();
    self.startPrewarmPoll();
};

/* 构建单个网格项(图片不设 src,由 IO 按视口加载/释放) */
Gallery.prototype.buildItem = function (item) {
    var self = this;
    var file = item.file;
    var index = item.index;

    var $item = $('<div class="gallery-item" data-index="' + index + '"></div>');
    if (file.type === 'video') {
        // 视频无缩略图,显示占位 + 播放图标,不加载视频本体
        $item.append('<div class="gallery-video-thumb"></div>');
        $item.append('<span class="gallery-video-icon">&#9654;</span>');
    } else {
        var thumb = self.thumbUrl(file, 'grid');
        var $img = $('<img alt="" decoding="async" />')
            .attr('data-src', thumb)
            .attr('data-orig', file.url);
        if (!self._ioSupported) {
            // 降级路径:直接加载(浏览器自行懒加载)
            $img.attr('src', thumb).attr('loading', 'lazy');
        }
        $img.on('error', function () {
            var img = this;
            var $i = $(img);
            /* 主动取消（滚出视口/收起目录/重进相册）之后浏览器仍可能补发一次
             * error。那不是真实失败：不能触发重试，也不能再动计数。 */
            if (img._cancelled) { return; }
            var n = $i.data('retry') || 0;
            if (n < Gallery.THUMB_RETRY) {
                // 相机端冷生成时偶发返回空 body(convert 还没写完 CGI 就收尾),
                // 此时缓存其实已经生成好,重试一次基本必中 —— 不要动辄回退
                // 到 4-8MB 原图,手机上代价太大。
                // ★ 重试必须重新入队：原来直接改 src 会绕开 MAX_CONCURRENT。
                $i.data('retry', n + 1);
                $i.attr('data-src', self.thumbUrl(file, 'grid', n + 1));
                self.requeueThumb(img);
                return;
            }
            if ($i.data('fb')) {
                // 原图也失败:显示占位
                self.settleThumb(img);
                img._state = 'done';
                $i.removeAttr('src').addClass('gallery-img-fallback');
                /* ★ 2026-11-22 P1：这里必须自己继续调度。
                 * 触发顺序是「本通用 handler 先、_startThumb 的 .nxthumb handler 后」
                 * （后者是后绑的），所以票据已被上面这行结算掉；
                 * 等 .nxthumb handler 跑时 `img._thumbToken !== token` 直接 return，不会 pump。
                 * 两个在飞请求同时走到这里时，槽位全还了却没人发牌 —— 队列停住
                 * （实测：在飞 2→0，仍有 1 张 queued）。 */
                self.pumpThumbs();
            } else {
                // 缩略图失败:回退加载原图(相机端未部署 CGI 时保证可用)
                $i.data('fb', true);
                $i.attr('data-src', $i.attr('data-orig'));
                self.requeueThumb(img);
            }
        });
        $item.append($img);
        if (self._ioSupported) {
            self._obs.observe($img[0]);
        }
    }
    // 多选角标
    $item.append('<span class="gallery-check">&#10003;</span>');
    $item.attr('title', file.name);
    if (self.selection[index]) {
        $item.addClass('selected');
        $item.find('.gallery-check').addClass('checked');
    }
    (function (p) {
        $item.on('click', function (ev) {
            if (self.selectMode) {
                ev.stopPropagation();
                self.toggleSelect(p);
            } else {
                self.openLightbox(p);
            }
        });
    })(index);
    return $item;
};

/* 缩略图 URL:相机端 thumb CGI 不可用时退回原图
 * size: 'grid'(默认,网格缩略图) | 'full'(灯箱大图)
 * retry: 非 0 时附加 cache-buster,用于冷生成返回空后的重试 */
Gallery.prototype.thumbUrl = function (file, size, retry) {
    if (!Gallery.THUMB.enabled || file.type !== 'image') {
        return file.url;
    }
    var u;
    try {
        u = new URL(file.url);
    } catch (e) {
        return file.url;
    }
    var rel = decodeURIComponent(u.pathname.replace(/^\/DCIM\/?/, ''));
    if (!rel) {
        return file.url;
    }
    var cfg = (size === 'full') ? Gallery.THUMB.full : Gallery.THUMB.grid;
    var url = 'http://' + u.hostname + ':' + Gallery.THUMB.port +
           '/cgi-bin/thumb?f=' + encodeURIComponent(rel) +
           '&w=' + cfg.width + '&q=' + cfg.quality;
    if (retry) {
        url += '&_r=' + retry;
    }
    return url;
};

/* 视口观察:进入视口加载 src,滚出视口清空 src 释放解码内存 */
Gallery.prototype.initObserver = function () {
    var self = this;
    if (!self._ioSupported) {
        return;
    }
    self.removeObserver();

    // 图片观察器:rootMargin 大,提前 1.5 屏加载/释放
    self._obs = new IntersectionObserver(function (entries) {
        var i;
        for (i = 0; i < entries.length; i++) {
            var en = entries[i];
            var el = en.target;
            if (en.isIntersecting) {
                var $img = $(el);
                if (!el.getAttribute('src') && $(el).attr('data-src')) {
                    // 入队而非直接发起,由调度器按并发上限发放
                    self.enqueueThumb(el);
                }
            } else {
                // 滚出视口:释放解码内存(回退原图的不再释放,避免反复下载大图)
                var $out = $(el);
                // 排队中 / 在飞：主动结算，把并发位立刻还回去。
                // 在飞的部分原来只能干等 load/error，而 removeAttribute('src')
                // 通常不派发事件 —— 这就是网格永久卡死的来源。
                if (self.cancelThumb(el)) {
                    continue;
                }
                if (el._state === 'done' && !$out.data('fb') && el.getAttribute('src')) {
                    el._cancelled = true;
                    el.removeAttribute('src');
                    el._state = 'idle';
                    el._loaded = false; // 允许滚回时重新入队
                }
            }
        }
    }, {
        rootMargin: '150% 0px',
        threshold: 0.01
    });
};

/* ============ 缩略图请求限流队列 ============
 *
 * 状态机（每个 img 元素上，字段 img._state）：
 *   idle      无请求
 *   queued    在 _loadQueue 里等并发位
 *   inflight  已 setAttribute('src')，持有票据 img._thumbToken
 *   done      已成功显示，或已确认失败并落到占位
 *
 * 三条铁律：
 *   1. 只有 inflight 能减计数；一张图同一时刻只有一个票据
 *   2. 结算幂等：重复调用不改变计数
 *   3. 取消不依赖浏览器事件 —— 直接清票据、解绑、还槽位
 *
 * 2026-11-22：旧版用单一 `_loading` 计数器 + `.one('load.nxthumb error.nxthumb')`。
 * 两个问题都会让网格永久卡死：
 *   · jQuery 的 .one() 对多事件是「每种各执行一次」，一次 error 后重试成功还能再减一次
 *   · 取消时 removeAttribute('src') 通常不派发 load/error，计数只增不减
 */

/* 入队：等待调度器发放请求 */
Gallery.prototype.enqueueThumb = function (img) {
    var self = this;
    if (img._state === 'queued' || img._state === 'inflight') {
        return;
    }
    if (img._state === 'done' && img._loaded) {
        return;
    }
    img._state = 'queued';
    img._loaded = false;
    img._cancelled = false;
    self._loadQueue.push(img);
    self.pumpThumbs();
};

/* 出队：只处理"还在队列里、尚未发起"的请求。返回 true 表示确实取消了一个。 */
Gallery.prototype.dequeueThumb = function (img) {
    var self = this;
    if (img._state !== 'queued') {
        return false;
    }
    var idx = self._loadQueue.indexOf(img);
    if (idx >= 0) {
        self._loadQueue.splice(idx, 1);
    }
    img._state = 'idle';
    return true;
};

/* 结算一张在飞的图：作废票据、解绑调度事件、释放并发位。幂等。 */
Gallery.prototype.settleThumb = function (img) {
    var self = this;
    var token = img._thumbToken;
    if (!token) {
        return false;
    }
    img._thumbToken = 0;
    if (self._inflight[token]) {
        delete self._inflight[token];
        self._inflightCount--;
        if (self._inflightCount < 0) { self._inflightCount = 0; }
    }
    $(img).off('.nxthumb');
    if (img._state === 'inflight') {
        img._state = 'idle';
    }
    self._loading = self._inflightCount;
    return true;
};

/* 取消：排队中的直接出队；在飞的主动结算并丢弃 src。 */
Gallery.prototype.cancelThumb = function (img) {
    var self = this;
    if (self.dequeueThumb(img)) {
        return true;
    }
    if (img._state === 'inflight') {
        img._cancelled = true;      // 抑制迟到的 error 重试
        self.settleThumb(img);
        img.removeAttribute('src');
        img._loaded = false;
        self.pumpThumbs();
        return true;
    }
    return false;
};

/* 失败后重新排队：先结算旧票据，再按 data-src 的新目标入队。 */
Gallery.prototype.requeueThumb = function (img) {
    var self = this;
    self.settleThumb(img);
    img._state = 'idle';
    img._loaded = false;
    img._cancelled = false;
    self.enqueueThumb(img);
};

/* 调度：在飞数不超过 MAX_CONCURRENT */
Gallery.prototype.pumpThumbs = function () {
    var self = this;
    var guard = 0;
    while (!self.downloading && self._inflightCount < Gallery.MAX_CONCURRENT &&
           self._loadQueue.length > 0 &&
           guard++ < 200) {
        var img = self._loadQueue.shift();
        if (img._state !== 'queued') {
            continue;
        }
        var src = $(img).attr('data-src');
        if (!src) {
            img._state = 'idle';
            continue;
        }
        self._startThumb(img, src);
    }
};

/* 发起一次受管请求。load/error 各绑一次，谁先到谁结算，另一个随即解绑。 */
Gallery.prototype._startThumb = function (img, src) {
    var self = this;
    var $img = $(img);
    var token = ++self._thumbSeq;

    img._thumbToken = token;
    img._state = 'inflight';
    img._cancelled = false;
    self._inflight[token] = img;
    self._inflightCount++;
    self._loading = self._inflightCount;

    $img.off('.nxthumb');
    $img.one('load.nxthumb', function () {
        if (img._thumbToken !== token) { return; }
        self.settleThumb(img);
        img._state = 'done';
        img._loaded = true;
        self.pumpThumbs();
    });
    $img.one('error.nxthumb', function () {
        if (img._thumbToken !== token) { return; }
        // 重试/回退由 buildItem 的 error handler 决定；这里只负责还槽位。
        self.settleThumb(img);
        self.pumpThumbs();
    });
    img.setAttribute('src', src);
};

/* 清空队列（刷新相册 / 切换相机时调用）。
 * 在飞的必须逐个作废并摘掉 src：旧视图的回调会污染新会话，
 * 而旧版直接把 `_loading = 0` 配上迟到的 load 事件会让计数变负数 ——
 * 负值下限流形同不存在，几十个请求会同时打到单核相机上。 */
Gallery.prototype.resetThumbQueue = function () {
    var self = this;
    var i, token;
    for (i = 0; i < self._loadQueue.length; i++) {
        var q = self._loadQueue[i];
        if (q._state === 'queued') { q._state = 'idle'; }
        q._cancelled = true;
    }
    self._loadQueue = [];
    for (token in self._inflight) {
        if (!self._inflight.hasOwnProperty(token)) { continue; }
        var img = self._inflight[token];
        img._thumbToken = 0;
        img._state = 'idle';
        img._loaded = false;
        img._cancelled = true;
        $(img).off('.nxthumb');
        img.removeAttribute('src');
    }
    self._inflight = {};
    self._inflightCount = 0;
    self._loading = 0;
    self._thumbSeq = 0;
};

/* 注销观察器(刷新/重进相册时调用) */
Gallery.prototype.removeObserver = function () {
    var self = this;
    if (self._obs) {
        self._obs.disconnect();
        self._obs = null;
    }
};

/* ============ 缩略图预热进度 ============ */

/* 相机端 thumb-prewarm.sh 在 web 遥控开启时后台铺缓存(单核 ARM 上
 * 0.5-2s/张)。没有提示时用户只能对着空白网格干等,以为是卡住了 ——
 * 这里把后台进度显示成"预热中 87/149",并在结束后自动停止轮询
 * (不白占相机 CPU 和 WiFi 带宽)。 */
/* 2026-11-22 实机结论：NX500 原片自带 320px(IFD1) 与 1616px(MPF) 内嵌图，
 * thumb-cgi 直切字节就够了。预热 worker 跑 62 张的结果是
 *   generated=0  sliced=62  skipped=0  failed=0
 * —— 纯空转，还要每 4s 占一次 8080 请求（最多 90 次）。
 * 默认关掉。真要预热（导入外机照片/PNG）时置 true，
 * 或手动跑 /opt/usr/nx-ks/nx-rc/thumb/thumb-prewarm.sh start。 */
Gallery.PREWARM_ENABLED = false;
Gallery.PREWARM_INTERVAL = 4000;   // 轮询间隔(ms)
Gallery.PREWARM_MAX_POLL = 90;     // 最多查 90 次(约 6 分钟)后放弃

Gallery.prototype.startPrewarmPoll = function () {
    var self = this;
    self.stopPrewarmPoll();
    if (!Gallery.PREWARM_ENABLED) { return; }
    var n = 0;
    var tick = function () {
        if (self.downloading) return;
        n++;
        var url = 'http://' + app.hostname + ':' + Gallery.THUMB.port +
                  '/cgi-bin/prewarm';
        $.ajax({url: url, dataType: 'json', cache: false, timeout: 5000})
            .done(function (d) {
                var $p = $('.gallery-prewarm');
                if (!$p.length) {
                    self.stopPrewarmPoll();
                    return;
                }
                if (!d || !d.total) {
                    $p.text('');
                    // 还没有进度数据:预热可能尚未启动或 CGI 未部署,
                    // 连试 3 次仍无数据就放弃,不做无谓轮询
                    if (n >= 3) {
                        self.stopPrewarmPoll();
                    }
                    return;
                }
                var cached = d.done + d.skipped;
                $p.text(d.running ? ('预热中 ' + cached + '/' + d.total)
                                  : ('已缓存 ' + cached + '/' + d.total));
                if (!d.running || n >= Gallery.PREWARM_MAX_POLL) {
                    self.stopPrewarmPoll();
                }
            })
            .fail(function () {
                // CGI 未部署(旧版相机端):静默放弃,不影响相册使用
                if (n >= 3) {
                    self.stopPrewarmPoll();
                }
            });
    };
    tick();
    self._prewarmTimer = setInterval(tick, Gallery.PREWARM_INTERVAL);
};

Gallery.prototype.stopPrewarmPoll = function () {
    if (this._prewarmTimer) {
        clearInterval(this._prewarmTimer);
        this._prewarmTimer = null;
    }
};

/* 顶部工具条 */
Gallery.prototype.buildToolbar = function () {
    var self = this;
    var $bar = $('<div class="gallery-toolbar"></div>');
    $bar.append($('<span class="gallery-count"></span>').text('共 ' + self.media.length + ' 个文件'));
    // 预热进度:后台在铺缩略图缓存时显示"预热 87/149",让用户知道在忙,
    // 而不是对着空白网格以为卡住了
    var $prewarm = $('<span class="gallery-prewarm"></span>');
    $bar.append($prewarm);
    var $refresh = $('<button type="button" class="btn btn-xs btn-default">刷新</button>')
        .on('click', function () {
            // 必须带 force:否则 2 分钟 TTL 内命中目录树缓存,
            // 点了刷新却什么都不重抓,拍完新照片看不到(2026-09-04 修复)
            self.init(self.rootUrl, {force: true});
        });
    var $orig = $('<button type="button" class="btn btn-xs btn-default">原始目录视图</button>')
        .on('click', function () {
            self.showIframeView();
        });

    var $select = $('<button type="button" class="btn btn-xs btn-info">选择</button>')
        .on('click', function () {
            self.setSelectMode(!self.selectMode);
        });
    var $selectAll = $('<button type="button" class="btn btn-xs btn-default" style="display:none;">全选</button>')
        .on('click', function () {
            self.selectAll();
        });
    var $download = $('<button type="button" class="btn btn-xs btn-success" style="display:none;">下载选中</button>')
        .on('click', function () {
            self.downloadSelected();
        });
    var $cancel = $('<button type="button" class="btn btn-xs btn-default" style="display:none;">取消</button>')
        .on('click', function () {
            self.setSelectMode(false);
        });
    var $dlInfo = $('<span class="gallery-dl-info" style="display:none;"></span>');
    var $stopDownload = $('<button type="button" class="btn btn-xs btn-warning" style="display:none;">停止下载</button>')
        .on('click', function () { self._downloadCancelled = true; if (self._downloadXhr) self._downloadXhr.abort(); });
    var $retryDownload = $('<button type="button" class="btn btn-xs btn-default" style="display:none;">重试失败</button>')
        .on('click', function () { self.downloadSelected(self._failedDownloads || []); });

    $bar.append($refresh).append($orig).append($select)
        .append($selectAll).append($download).append($cancel)
        .append($stopDownload).append($retryDownload).append($dlInfo);
    self._selectBtn = $select;
    self._selectAllBtn = $selectAll;
    self._downloadBtn = $download;
    self._cancelBtn = $cancel;
    self._dlInfo = $dlInfo;
    self._stopDownload = $stopDownload;
    self._retryDownload = $retryDownload;
    return $bar;
};

/* 进入/退出多选模式 */
Gallery.prototype.setSelectMode = function (on) {
    var self = this;
    self.selectMode = on;
    if (on) {
        self._selectBtn.text('取消选择').removeClass('btn-info').addClass('btn-default');
        self._selectAllBtn.show();
        self._downloadBtn.show();
        self._cancelBtn.show();
    } else {
        self._selectBtn.text('选择').removeClass('btn-default').addClass('btn-info');
        self._selectAllBtn.hide();
        self._downloadBtn.hide();
        self._cancelBtn.hide();
        self.selection = {};
    }
    self.updateItemStates();
    self.updateDownloadCount();
};

Gallery.prototype.toggleSelect = function (index) {
    var self = this;
    if (self.selection[index]) {
        delete self.selection[index];
    } else {
        self.selection[index] = true;
    }
    self.updateItemStates();
    self.updateDownloadCount();
};

Gallery.prototype.selectAll = function () {
    var self = this;
    var i;
    for (i = 0; i < self.media.length; i++) {
        self.selection[i] = true;
    }
    self.updateItemStates();
    self.updateDownloadCount();
};

Gallery.prototype.updateItemStates = function () {
    var self = this;
    $('#gallery .gallery-item').each(function () {
        var idx = $(this).data('index');
        if (self.selectMode) {
            $(this).addClass('selectable');
        } else {
            $(this).removeClass('selectable');
        }
        if (self.selection[idx]) {
            $(this).addClass('selected');
            $(this).find('.gallery-check').addClass('checked');
        } else {
            $(this).removeClass('selected');
            $(this).find('.gallery-check').removeClass('checked');
        }
    });
};

Gallery.prototype.updateDownloadCount = function () {
    var self = this;
    var n = Object.keys(self.selection).length;
    if (self._downloadBtn) {
        self._downloadBtn.text(n > 0 ? ('下载选中 (' + n + ')') : '下载选中');
    }
};

/* 灯箱由浏览器直接保存附件，页面无需读取跨域原图响应。 */
Gallery.prototype.downloadUrl = function (file) {
    try {
        var url = new URL(file.url);
        if (!/^https?:$/.test(url.protocol) || url.pathname.indexOf('/DCIM/') !== 0) {
            return null;
        }
        var rel = Gallery.dirRelPath(url.pathname);
        if (!rel) {
            return null;
        }
        url.port = Gallery.THUMB.port;
        url.pathname = '/cgi-bin/download';
        url.search = 'f=' + encodeURIComponent(rel);
        url.hash = '';
        return url.href;
    } catch (e) {
        return null;
    }
};

/* 单个文件的下载分片：4 MiB 一片。
 * 2026-11-22 实机实测（10.3MB 原片，Wi-Fi 1.8MB/s）：每片固定开销约 0.4s
 * （CGI 启动 + 打开文件 + stat），所以片越小总耗时越长：
 *   2 MiB × 6 片 = 9.0s ｜ 4 MiB × 3 片 = 6.0s ｜ 整块 1 次 = 6.0s
 * 4 MiB 与整块持平，同时断线最多只重传 4MB —— 就取它。 */
Gallery.DOWNLOAD_CHUNK = 4 * 1024 * 1024;
Gallery.DOWNLOAD_TRIES = 3;
/* 超过这个大小就不再走「整块读进 JS 堆」的路径。
 * arraybuffer + Uint8Array(total) 会一次性分配 total 字节的 JS 堆内存；
 * 相机 4K 录像动辄 1-2GB，手机浏览器分配这个量级会直接失败或把页面搞崩。
 * 原实现用 responseType='blob'，数据由浏览器自己管（可落盘），不受此限。
 * 所以：小文件走分片（可续传），大文件退回 blob 单请求（有超时、无续传）。 */
Gallery.DOWNLOAD_MAX_RESUME = 64 * 1024 * 1024;

/* 批量下载的单文件步骤。
 *
 * 2026-11-22 修四个会让"批量下载卡死"的问题：
 *   1. 原来直接拉 file.url（80 端口 daemon 的原图），绕开了 8080 的
 *      download-cgi —— 后者支持 Range/ETag/304，也不与遥控心跳抢 daemon。
 *   2. 从未设置 xhr.timeout，ontimeout 是死钩子。相机 WiFi 半开时这个 XHR
 *      既不 load 也不 error，批量队列就永远停在"正在下载 3/20：45%"。
 *   3. 只判 status===200 就保存，HTML 错误页会被当成照片存下来。
 *   4. 外层无差别重试 3 次、每次从头拉。8MB 原图在弱 WiFi 上一次下完的
 *      概率很低，重试等于把同样的 8MB 再赌一遍。
 *
 * 现在的流程：
 *   HEAD 探能力（不传 body，避免为一探白下一整份）→ 大文件走分片 Range，
 *   断线只重试当前片；服务端不认 Range 就退化成整块；404/403/416 不重试。
 *   片与片之间要求严格首尾相接（start === nextStart 且长度吻合），
 *   所以拼接不可能错位。
 */
Gallery.prototype.downloadOne = function (file, done) {
    var self = this;
    var url = self.downloadUrl(file) || file.url;
    var chunkSize = Gallery.DOWNLOAD_CHUNK;
    var total = 0;        // 总字节，0 = 未知
    var buf = null;       // Uint8Array(total)，收到即写入，不额外留存分片副本
    var nextStart = 0;    // 已收字节数 = 下一片起点
    var tries = 0;        // 当前片的尝试次数
    var mode = 'probe';   // probe | ranged | whole
    var expectEtag = '';  // HEAD 给出的版本标识；分片据此拒绝跨版本拼接
    var settled = false;

    function finish(ok) {
        if (settled) { return; }
        settled = true;
        self._downloadXhr = null;
        done(ok);
    }

    function saveBlob(blob) {
        var objUrl = URL.createObjectURL(blob);
        var a = document.createElement('a');
        a.href = objUrl;
        var dlName = file.name;
        if (file.dir && file.dir !== '根目录') {
            dlName = file.dir + '_' + file.name;
        }
        a.download = dlName;
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        setTimeout(function () { URL.revokeObjectURL(objUrl); }, 5000);
        finish(true);
    }

    function save() {
        if (!total || nextStart !== total || !buf) { finish(false); return; }
        var all = buf;
        /* 错误页几乎总是 HTML/JSON，正常媒体不会以 '<' 或 '{"' 开头。
         * 不做完整魔数白名单 —— 那会误伤少见的容器格式。 */
        if (all.length < 2 || all[0] === 0x3C ||
            (all[0] === 0x7B && all[1] === 0x22)) {
            finish(false);
            return;
        }
        saveBlob(new Blob([all], { type: 'application/octet-stream' }));
    }

    function retry() {
        if (self._downloadCancelled) { finish(false); return; }
        if (tries < Gallery.DOWNLOAD_TRIES) {
            setTimeout(step, 400 * tries);
            return;
        }
        if (mode === 'ranged') {
            /* 分片彻底失败：可能是跨域预检被拒（相机 busybox httpd 不一定
             * 答 OPTIONS）。退回整块，至少还能下完 —— 只是没有断点续传。 */
            mode = 'whole';
            buf = null;
            nextStart = 0;
            total = 0;
            tries = 0;
            setTimeout(step, 0);
            return;
        }
        finish(false);
    }

    function step() {
        if (settled) { return; }
        if (self._downloadCancelled) { finish(false); return; }
        tries++;

        var end = nextStart + chunkSize - 1;
        if (total > 0 && end > total - 1) { end = total - 1; }

        var useBlob = (mode === 'blob');
        var target = url;
        if (!useBlob && mode === 'ranged' && total > 0) {
            // 分片参数走 query，不用 Range 头 —— busybox httpd 不把它转给 CGI
            target = url + (url.indexOf('?') >= 0 ? '&' : '?') +
                     'b=' + nextStart + '&e=' + end;
        }
        var xhr = new XMLHttpRequest();
        self._downloadXhr = xhr;
        xhr.open('GET', target, true);
        xhr.responseType = useBlob ? 'blob' : 'arraybuffer';
        xhr.timeout = useBlob ? 300000 : 90000;

        // 无进展看门狗：45s 没有一个字节就主动放弃，让本片重来。
        // 只靠 xhr.timeout 不够 —— 半开连接上 progress 会一直有微量推进。
        var lastAt = Date.now();
        var watchdog = setInterval(function () {
            if (Date.now() - lastAt > 45000) {
                clearInterval(watchdog);
                try { xhr.abort(); } catch (e) { /* 已结束 */ }
            }
        }, 5000);
        function settleTimer() { clearInterval(watchdog); }

        xhr.onprogress = function (e) {
            lastAt = Date.now();
            if (e.lengthComputable && total > 0) {
                self._dlInfo.text('正在下载 ' + self._downloadPosition + '：' +
                    Math.floor((nextStart + e.loaded) * 100 / total) + '%');
            }
        };

        xhr.onload = function () {
            settleTimer();
            var code = xhr.status;
            if (code === 404 || code === 403 || code === 416) {
                // 文件不存在 / 无权限 / 区间不可满足：重试没有意义
                finish(false);
                return;
            }
            if (code !== 200 && code !== 206) { retry(); return; }

            /* 大文件：blob 由浏览器自己管，不分配 JS 堆。
             * 代价是没有断点续传，靠 300s 超时 + 3 次整体重试。 */
            if (useBlob) {
                var b = xhr.response;
                if (!b || !b.size) { retry(); return; }
                if (b.size < 512) {
                    // 小响应才可能是错误页，读首两字节确认；大响应直接保存
                    b.slice(0, 2).arrayBuffer().then(function (ab) {
                        var h = new Uint8Array(ab);
                        if (h.length >= 2 &&
                            (h[0] === 0x3C || (h[0] === 0x7B && h[1] === 0x22))) {
                            finish(false);
                        } else {
                            saveBlob(b);
                        }
                    }, function () { saveBlob(b); });
                    return;
                }
                saveBlob(b);
                return;
            }

            var data = new Uint8Array(xhr.response || new ArrayBuffer(0));

            /* 分片模式只核对长度：服务端按 b/e 切，Content-Length 就是段长。
             * 不依赖 206 / Content-Range —— 本机 httpd 不透传 Range 头。 */
            if (mode === 'ranged') {
                /* ★ 2026-11-22 P2：先验版本，再比长度。
                 * 同名文件在下载期间被替换时，长度合法但属于不同版本的片段
                 * 仍会被拼进去并报告成功（静默损坏）。HEAD 与各片都带 ETag 时拒绝。 */
                var gotEtag = (xhr.getResponseHeader('ETag') || '').trim();
                if (expectEtag && gotEtag && gotEtag !== expectEtag) {
                    finish(false);
                    return;
                }
                var want = end - nextStart + 1;
                if (data.byteLength === want) {
                    if (!buf || buf.length !== total) { buf = new Uint8Array(total); }
                    buf.set(data, nextStart);
                    nextStart = end + 1;
                    tries = 0;
                    if (nextStart >= total) { save(); return; }
                    setTimeout(step, 0);
                    return;
                }
                if (data.byteLength === total && nextStart === 0) {
                    // CGI 不认 b/e（旧版），返回了全文：转整块，别再白试
                    mode = 'whole';
                    buf = new Uint8Array(total);
                    buf.set(data, 0);
                    nextStart = data.byteLength;
                    save();
                    return;
                }
                retry();
                return;
            }

            /* whole：一份就是全文 */
            var len = parseInt(xhr.getResponseHeader('Content-Length') || '0', 10);
            var declared = len > 0 ? len : data.byteLength;
            if (data.byteLength !== declared) { retry(); return; }
            total = declared;
            buf = new Uint8Array(declared);
            buf.set(data, 0);
            nextStart = data.byteLength;
            save();
        };

        xhr.onerror = function () { settleTimer(); retry(); };
        xhr.ontimeout = function () { settleTimer(); retry(); };
        xhr.onabort = function () {
            settleTimer();
            if (self._downloadCancelled) { finish(false); return; }
            retry();   // 看门狗中止：本片重来
        };
        xhr.send();
    }

    function probe() {
        var xhr = new XMLHttpRequest();
        self._downloadXhr = xhr;
        xhr.open('HEAD', url, true);
        xhr.timeout = 15000;
        xhr.onload = function () {
            var code = xhr.status;
            if (code === 404 || code === 403) { finish(false); return; }
            var len = parseInt(xhr.getResponseHeader('Content-Length') || '0', 10);
            /* 用 X-Accept-Chunk 而不是 Accept-Ranges：busybox httpd 不把 Range
             * 头传给 CGI（实测 HTTP_RANGE 为空），所以标准 206 走不通；
             * 分片是靠 query 参数 b/e 实现的，能力由这个自定义头声明。 */
            var chunkable = /^1$/.test((xhr.getResponseHeader('X-Accept-Chunk') || '').trim());
            /* HEAD 的版本标识：分片模式下每片都要比对，防止同名文件被替换后
               把两个版本的片段拼成"长度合法但内容错乱"的文件（静默损坏）。 */
            expectEtag = (xhr.getResponseHeader('ETag') || '').trim();
            /* ★ 2026-11-22 P2：len 无效（HEAD 405 被拒 / 无 Content-Length）时不能进 whole。
             * whole 走 arraybuffer，未知大小的视频会整份进 JS 堆，绕过 DOWNLOAD_MAX_RESUME
             * 的 64MiB 保护。这种情况和 onerror/ontimeout 一样交给浏览器流式管（blob）。 */
            if (!(len > 0)) {
                mode = 'blob';
            } else if (len > Gallery.DOWNLOAD_MAX_RESUME) {
                // 太大，不能整块拼进 JS 堆：交给浏览器自己管（blob）
                mode = 'blob';
            } else if (chunkable && len > chunkSize) {
                mode = 'ranged';
                total = len;
            } else {
                mode = 'whole';
            }
            step();
        };
        // HEAD 拿不到大小（网络问题/HEAD 被拒且无 Content-Length）时，
        // 无从判断文件多大 —— 选不分配 JS 堆的 blob 更安全。
        xhr.onerror = function () { mode = 'blob'; step(); };
        xhr.ontimeout = function () { mode = 'blob'; step(); };
        xhr.onabort = function () {
            if (self._downloadCancelled) { finish(false); return; }
            mode = 'blob';
            step();
        };
        xhr.send();
    }

    probe();
};

/* 批量下载:串行逐张下载(避免并发打满相机 WiFi) */
Gallery.prototype.downloadSelected = function (retryFiles) {
    var self = this;
    var files = Array.isArray(retryFiles) ? retryFiles.slice() : Object.keys(self.selection).map(function (index) { return self.media[index]; });
    var i = 0;
    var ok = 0;
    var failed = [];
    if (files.length === 0 || self.downloading) {
        return;
    }
    self.downloading = true;
    self._downloadCancelled = false;
    self._failedDownloads = [];
    self._retryDownload.hide();
    self._stopDownload.show();
    self._dlInfo.show().text('正在下载 0/' + files.length + ' ...');
    self._downloadBtn.prop('disabled', true);

    function next() {
        if (i >= files.length || self._downloadCancelled) {
            if (self._downloadCancelled) failed = failed.concat(files.slice(i));
            self.downloading = false;
            self.pumpThumbs();
            self._downloadXhr = null;
            self._stopDownload.hide();
            self._downloadBtn.prop('disabled', false);
            self._failedDownloads = failed;
            if (failed.length) self._retryDownload.show().text('重试失败 (' + failed.length + ')');
            var msg = (self._downloadCancelled ? '已停止' : '下载完成') + ':成功 ' + ok + (failed.length ? (',未完成 ' + failed.length) : '');
            self._dlInfo.text(msg);
            return;
        }
        var file = files[i];
        self._downloadPosition = (i + 1) + '/' + files.length;
        self._dlInfo.text('正在下载 ' + self._downloadPosition + ' ...');
        /* 重试与续传都在 downloadOne 内部完成 —— 只有那里知道已经收了多少
         * 字节。外层只负责推进队列，避免同一次失败被内外两层各重试一遍。 */
        self.downloadOne(file, function (success) {
            if (success) { ok++; } else { failed.push(file); }
            i++;
            next();
        });
    }
    next();
};

/* 降级:以 iframe 打开相机端原始目录页 */
Gallery.prototype.showIframeView = function () {
    var self = this;
    var $g = $('#gallery').empty();
    var $bar = $('<div class="gallery-toolbar"></div>');
    $bar.append($('<button type="button" class="btn btn-xs btn-default">返回缩略图视图</button>')
        .on('click', function () {
            // force:原始目录视图期间可能拍了对新照片,
            // 返回时不能沿用旧目录树缓存
            self.init(self.rootUrl, {force: true});
        }));
    $g.append($bar);

    var $iframe = $('<iframe id="galleryContent"></iframe>').attr('src', self.rootUrl);
    $g.append($iframe);

    function resize_iframe() {
        var height = window.innerWidth; // Firefox
        if (document.body.clientHeight) {
            height = document.body.clientHeight; // IE
        }
        if (self.iframe != null) {
            self.iframe.css('height', (height - 50) + 'px');
        }
    }
    self.iframe = $iframe;
    resize_iframe();
    window.onresize = resize_iframe;
};

/* ============ Lightbox 查看原图 ============ */
Gallery.prototype.initLightbox = function () {
    var self = this;
    if (self.lightbox) {
        return;
    }
    var $lb = $('<div id="galleryLightbox" class="gallery-lightbox"></div>');
    $lb.append('<div class="gallery-lb-toolbar">' +
               '<span class="gallery-lb-counter"></span>' +
               '<button type="button" class="gallery-lb-orig" title="加载原始尺寸(4-8MB,较慢)" style="display:none;">查看原图</button>' +
               '<a class="gallery-lb-dl" title="下载当前文件" aria-label="下载当前文件">' +
               '<i class="fa fa-download"></i></a>' +
               '<button type="button" class="gallery-lb-close" title="关闭 (Esc)">&times;</button>' +
               '</div>');
    $lb.append('<div class="gallery-lb-stage">' +
               '<div class="gallery-lb-spinner"></div>' +
               '<img class="gallery-lb-media gallery-lb-preview" alt="" style="display:none;" />' +
               '<img class="gallery-lb-media gallery-lb-image" alt="" fetchpriority="high" />' +
               '<video class="gallery-lb-media gallery-lb-video" controls preload="metadata"></video>' +
               '</div>');
    $lb.append('<button type="button" class="gallery-lb-nav gallery-lb-prev" title="上一张 (&#8592;)">&#8249;</button>');
    $lb.append('<button type="button" class="gallery-lb-nav gallery-lb-next" title="下一张 (&#8594;)">&#8250;</button>');
    $lb.hide();
    $('body').append($lb);
    self.lightbox = $lb;

    // 关闭
    $lb.find('.gallery-lb-close').on('click', function () {
        self.closeLightbox();
    });
    // 查看原图:灯箱默认显示 1024px 大图,需要细看/保存时再拉原图
    $lb.find('.gallery-lb-orig').on('click', function () {
        var file = self.media[self.current];
        if (!file || file.type !== 'image') {
            return;
        }
        var $o = $(this);
        var $img = $lb.find('.gallery-lb-image');
        $o.prop('disabled', true).text('加载中...');
        $lb.find('.gallery-lb-spinner').show();
        $img.data('fb', true).attr('src', file.url);
    });
    $lb.on('click', function (e) {
        if (e.target === this) {
            self.closeLightbox();
        }
    });
    // 导航
    $lb.find('.gallery-lb-prev').on('click', function () {
        self.navigate(-1);
    });
    $lb.find('.gallery-lb-next').on('click', function () {
        self.navigate(1);
    });
    // 键盘
    $(document).on('keydown', function (e) {
        if (!self.lightbox || !self.lightbox.is(':visible')) {
            return;
        }
        if (e.keyCode === 27) {
            self.closeLightbox();
        } else if (e.keyCode === 37) {
            self.navigate(-1);
        } else if (e.keyCode === 39) {
            self.navigate(1);
        }
    });
    // 触摸滑动
    $lb.on('touchstart', function (e) {
        var t = e.originalEvent.touches;
        if (t && t.length > 0) {
            self.touchStartX = t[0].clientX;
        }
    });
    $lb.on('touchend', function (e) {
        var t = e.originalEvent.changedTouches;
        if (!t || t.length === 0) {
            return;
        }
        var dx = t[0].clientX - self.touchStartX;
        if (Math.abs(dx) > 40) {
            if (dx < 0) {
                self.navigate(1);
            } else {
                self.navigate(-1);
            }
        }
    });
};

Gallery.prototype.openLightbox = function (index) {
    var self = this;
    if (index < 0 || index >= self.media.length) {
        return;
    }
    self.initLightbox();
    self.current = index;
    self.updateLightbox();
    self.lightbox.show();
    $('body').css('overflow', 'hidden');
};

Gallery.prototype.closeLightbox = function () {
    var self = this;
    if (self.lightbox) {
        self.lightbox.hide();
        self.lightbox.find('.gallery-lb-image').off('.lb').stop(true, true).removeAttr('src');
        self.lightbox.find('.gallery-lb-preview').removeAttr('src');
        self.lightbox.find('.gallery-lb-video').attr('src', '');
    }
    self.current = -1;
    $('body').css('overflow', '');
};

Gallery.prototype.navigate = function (delta) {
    var self = this;
    var n = self.media.length;
    if (n === 0) {
        return;
    }
    self.current = (self.current + delta + n) % n;
    self.updateLightbox();
};

Gallery.prototype.updateLightbox = function () {
    var self = this;
    if (!self.lightbox) {
        return;
    }
    var file = self.media[self.current];
    var $lb = self.lightbox;
    var $counter = $lb.find('.gallery-lb-counter');
    var $spinner = $lb.find('.gallery-lb-spinner');
    var $preview = $lb.find('.gallery-lb-preview');
    var $img = $lb.find('.gallery-lb-image');
    var $video = $lb.find('.gallery-lb-video');
    var $origBtn = $lb.find('.gallery-lb-orig');

    $counter.text((self.current + 1) + ' / ' + self.media.length);
    $lb.find('.gallery-lb-dl').attr('href', self.downloadUrl(file));
    $img.off('.lb').stop(true, true).hide().removeAttr('src');
    $preview.hide().removeAttr('src');
    $video.hide().attr('src', '');
    $origBtn.hide();

    if (file.type === 'video') {
        $spinner.hide();
        $video.attr('src', file.url).show();
    } else {
        var src = self.thumbUrl(file, 'full');
        var grid = $('#gallery .gallery-item[data-index="' + self.current + '"] img')[0];
        var preview = grid && grid.complete && grid.naturalWidth > 0 && grid.currentSrc !== src;
        if (preview) {
            $preview.attr('src', grid.currentSrc).show();
        }
        $spinner.toggle(!preview);
        $img.data('fb', src === file.url);
        $img.on('load.lb', function () {
            $spinner.hide();
            $preview.hide().removeAttr('src');
            $img.fadeIn(120);
            // 非原图时才显示"原图"入口(已是原图就不必再点)
            if ($img.data('fb')) {
                $origBtn.hide();
            } else {
                $origBtn.show().prop('disabled', false).text('查看原图');
            }
        });
        $img.on('error.lb', function () {
            if (!$img.data('fb')) {
                // 灯箱大图失败(未预热/生成失败):回退原图再试一次
                $img.data('fb', true);
                $img.attr('src', file.url);
                return;
            }
            $spinner.hide();
            $img.off('.lb').removeAttr('src');
            $counter.text($counter.text() + ' (加载失败)');
        });
        // 相邻照片等用户翻到再加载，避免抢占当前预览的生成和传输。
        $img.attr('src', src);
    }
};
