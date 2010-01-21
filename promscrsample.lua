-- function called before the database is opened.
-- No parameter is given.
-- The return values are shown as the information paragraphs.
function _begin()
   local name = "World!!"
   if _user then
      name = _user.name
   end
   return "Hello, " .. name .. "!!"
end


-- Function called after the database is closed.
-- No parameter is given.
-- The return values are shown as the information paragraphs.
function _end()
   local msg = "Knowledge comes, but wisdom lingers."
   if _params.act == "edit" then
      msg = "Tiger, Fire, Cyber, Fiber, Diver, Viber, Jaa Jaa."
   elseif _params.act == "search" then
      msg = "Do not say a little in many words but a great deal in a few."
   end
   return msg
end


-- Function called to convert the Wiki data of each article.
-- `wiki' specifies the Wiki string of the article.
-- The return value is the result data or `nil' if no modification.
function _procart(wiki)
   wiki = _strstr(wiki, "Tokyo", "Osaka")
   if _regex(wiki, "*sex") or _regex(wiki, "*fuck") then
      wiki = _regex(wiki, "^#!", "#! [sexual] ")
   end
   return wiki
end


-- Function called to convert the HTML data of the whole page.
-- `html' specifies the HTML string of the page.
-- The return value is the result data or `nil' if no modification.
function _procpage(html)
   html = _regex(html, '<p( +[a-z]+="[^"]*")* *>', "&<i>")
   html = _regex(html, "</p>", "</i>&")
   return html
end
