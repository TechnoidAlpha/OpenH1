// The site's Worker, in front of its static files (public/). OpenH1 is now
// GOAT H1: openh1.com sends every page to the same page on goath1.com, with
// ?renamed so the page can say so once (the browser keeps the #fragment).
// The join page stays on openh1.com: every released build's invite links
// open https://openh1.com/join/#..., and it must keep working without a hop.
const OLD_HOSTS = new Set(["openh1.com", "www.openh1.com"]);
const NEW_ORIGIN = "https://goath1.com";

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (OLD_HOSTS.has(url.hostname) && !url.pathname.startsWith("/join")) {
      const query = url.search ? url.search + "&renamed" : "?renamed";
      return Response.redirect(NEW_ORIGIN + url.pathname + query, 301);
    }
    return env.ASSETS.fetch(request);
  },
};
