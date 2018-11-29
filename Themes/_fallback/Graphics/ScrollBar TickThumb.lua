local t = Def.ActorFrame {}
local whee
t[#t + 1] =
	Def.ActorFrame {
	Def.Quad {
		Name = "DootyMcBooty",
		BeginCommand = function(self)
			self:zoomto(32, 32):valign(0.634522134234)
			if SCREENMAN:GetTopScreen():GetName() == "ScreenSelectMusic" or SCREENMAN:GetTopScreen():GetName() == "ScreenNetSelectMusic" then
				whee = SCREENMAN:GetTopScreen():GetMusicWheel()
			end
		end,
		LeftClickMessageCommand = function(self)
			if SCREENMAN:GetTopScreen():GetName() == "ScreenSelectMusic" or SCREENMAN:GetTopScreen():GetName() == "ScreenNetSelectMusic" then
				if SCREEN_WIDTH - INPUTFILTER:GetMouseX() < 32 then
					local idx = whee:GetCurrentIndex()
					local num = whee:GetNumItems()
					local dum = (INPUTFILTER:GetMouseY() - 45) / (SCREEN_HEIGHT - 103)
					whee:Move(notShit.round(num * dum) - idx)
				end
			end
		end
	}
}

return t