.pragma library

// A document stylesheet also covers images inserted by lazy loading or an
// in-page search/filter change. No image URLs, loading rules, or sizes change.
var imagePaintScript = `(function () {
    if (!/^(?:[a-z0-9-]+\\.)?google\\.(?:com|[a-z]{2,3}|(?:com|co)\\.[a-z]{2})$/i.test(location.hostname)
        || !/^\\/(?:search(?:\\/|$)|webhp(?:\\/|$)|imgres(?:\\/|$)|$)/.test(location.pathname)) return;
    if (!document.head || document.getElementById('ocean-image-paint')) return;
    var style = document.createElement('style');
    style.id = 'ocean-image-paint';
    // WPE's tiled renderer sometimes leaves successfully decoded images blank.
    // Request image layers without overriding the site's own CSS transforms.
    style.textContent = 'img { will-change: transform !important; }';
    document.head.appendChild(style);
})()`;
