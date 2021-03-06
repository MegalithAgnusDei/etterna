#include "global.h"
#include "NoteData.h"
#include "NotesLoaderOSU.h"
#include "RageFile.h"
#include "RageUtil_CharConversions.h"
#include "Song.h"
#include "Steps.h"

vector<string>
split(string str, string token)
{
	vector<string> result;
	while (str.size()) {
		int index = str.find(token);
		if (index != string::npos) {
			result.push_back(str.substr(0, index));
			str = str.substr(index + token.size());
			if (str.size() == 0)
				result.push_back(str);
		} else {
			result.push_back(str);
			str = "";
		}
	}
	return result;
}

map<string, map<string, string>>
OsuLoader::ParseFileString(string fileContents)
{
	vector<string> sections;
	vector<vector<string>> contents;

	SeparateTagsAndContents(fileContents, sections, contents);

	map<string, map<string, string>> parsedData;

	if (sections.size() == 7) {
		for (int i = 0; i < (int)sections.size(); ++i) {
			for (auto& content : contents[i]) {
				auto& str = content;
				int pos = str.find_first_of(':');
				string value = str.substr(pos + 1), tag = str.substr(0, pos);
				parsedData[sections[i]][tag] = value;
			}
		}
	}
	return parsedData;
}

void
OsuLoader::SeparateTagsAndContents(string fileContents,
								   vector<string>& tagsOut,
								   vector<vector<string>>& contentsOut)
{
	char lastByte = '\0';
	bool isComment = false;
	bool isTag = false;
	bool isContent = false;
	string tag = "";
	string content = "";

	int tagIndex = 0;

	for (int i = 0; i < (int)fileContents.length(); ++i) {
		char currentByte = fileContents[i];

		if (isComment || currentByte == '\r') // ignore carriage return
		{
			if (currentByte == '\n') {
				isComment = false;
			}
		} else if (currentByte == '/') {
			if (lastByte == '/') {
				isComment = true;
			}
		} else if (isTag) {
			if (currentByte == ']') {
				++tagIndex;
				tagsOut.emplace_back(tag);
				isTag = false;
				content = "";
				contentsOut.emplace_back(vector<string>());
				isContent = true;
			} else {
				tag = tag + currentByte;
			}
		} else if (isContent) {
			if ((currentByte == '[' && lastByte == '\n') ||
				i == (int)fileContents.length() - 1) {
				contentsOut.back().emplace_back(content);
				content = "";
				isContent = false;
				tag = "";
				isTag = true;
			} else if (currentByte == '\n') {
				contentsOut.back().emplace_back(content);
				content = "";
			} else {
				content = content + currentByte;
			}
		} else if (currentByte == '[') {
			tag = "";
			isTag = true;
		}
		lastByte = currentByte;
	}
}

void
OsuLoader::SetMetadata(map<string, map<string, string>> parsedData, Song& out)
{
	// set metadata values
	auto& metadata = parsedData["Metadata"];
	out.m_sSongFileName = metadata["Title"];
	out.m_sMainTitle = metadata["Title"];
	out.m_sSubTitle = metadata["Version"];
	out.m_sArtist = metadata["Artist"];
	out.m_sGenre = "";

	ConvertString(out.m_sMainTitle, "utf-8,english");
	ConvertString(out.m_sSubTitle, "utf-8,english");

	out.m_sMusicFile = parsedData["General"]["AudioFilename"];
	// out.m_sBackgroundFile = "";
	// out.m_sCDTitleFile = "";
}

void
OsuLoader::SetTimingData(map<string, map<string, string>> parsedData, Song& out)
{
	vector<pair<int, float>> tp;

	for (auto it = std::next(parsedData["TimingPoints"].begin());
		 it != parsedData["TimingPoints"].end();
		 ++it) {
		auto line = it->first;
		auto values = split(line, ",");

		tp.emplace_back(pair<int, float>(stoi(values[0]), stof(values[1])));
	}
	sort(tp.begin(), tp.end(), [](pair<int, float> a, pair<int, float> b) {
		return a.first < b.first;
	});

	vector<pair<int, float>> bpms;
	float lastpositivebpm = 0;
	int offset = 0;
	int lastoffset = -9999;
	for (auto x : tp) {
		float bpm;
		offset = max(0, x.first);
		if (x.second > 0) {
			bpm = 60000 / x.second;
			lastpositivebpm = bpm;
		} else // inherited timing points; these might be able to be ignored
		{
			bpm = lastpositivebpm * abs(x.second / 100);
		}
		if (offset == lastoffset) {
			bpms[bpms.size() - 1] =
			  pair<int, float>(offset, bpm); // this because of dumb stuff like
											 // in 4k Luminal dan (not robust,
											 // but works for most files)
		} else {
			bpms.emplace_back(pair<int, float>(offset, bpm));
		}
		lastoffset = offset;
	}
	if (bpms.size() > 0) {
		out.m_SongTiming.AddSegment(
		  BPMSegment(0, bpms[0].second)); // set bpm at beat 0 (osu files don't
										  // make this required)
	} else {
		out.m_SongTiming.AddSegment(BPMSegment(0, 120)); // set bpm to 120 if
														 // there are no
														 // specified bpms in
														 // the file (there
														 // should be)
	}
	for (int i = 0; i < (int)bpms.size(); ++i) {
		int row = MsToNoteRow(bpms[i].first, &out);
		if (row != 0) {
			out.m_SongTiming.AddSegment(BPMSegment(row, bpms[i].second));
		}
	}

	out.m_DisplayBPMType = DISPLAY_BPM_ACTUAL;

	auto general = parsedData["General"];
	out.m_fMusicSampleStartSeconds = stof(general["AudioLeadIn"]) / 1000.0f;
	out.m_fMusicSampleLengthSeconds =
	  stof(general["PreviewTime"]) / 1000.0f; // these probably aren't right
}

bool
OsuLoader::LoadChartData(Song* song,
						 Steps* chart,
						 map<string, map<string, string>> parsedData)
{
	if (stoi(parsedData["General"]["Mode"]) != 3) // if the mode isn't mania
	{
		return false;
	}

	switch (stoi(parsedData["Difficulty"]["CircleSize"])) {
		case (4): {
			chart->m_StepsType = StepsType_dance_single;
			break;
		}
		case (5): {
			chart->m_StepsType = StepsType_pump_single;
			break;
		}
		case (6): {
			chart->m_StepsType = StepsType_dance_solo;
			break;
		}
		case (7): {
			chart->m_StepsType = StepsType_kb7_single;
			break;
		}
		case (8): {
			chart->m_StepsType = StepsType_dance_double;
			break;
		}
		default:
			chart->m_StepsType = StepsType_Invalid;
			return false;
	} // needs more stepstypes?

	chart->SetMeter(song->GetAllSteps().size());

	chart->SetDifficulty(
	  (Difficulty)(min(song->GetAllSteps().size(), (size_t)Difficulty_Edit)));

	chart->TidyUpData();

	chart->SetSavedToDisk(true);

	return true;
}

void
OsuLoader::GetApplicableFiles(const RString& sPath, vector<RString>& out)
{
	GetDirListing(sPath + RString("*.osu"), out);
}

int
OsuLoader::MsToNoteRow(int ms, Song* song)
{
	float tempOffset = song->m_SongTiming.m_fBeat0OffsetInSeconds;
	song->m_SongTiming.m_fBeat0OffsetInSeconds = 0;

	int row = (int)round(abs(
	  song->m_SongTiming.GetBeatFromElapsedTimeNoOffset(ms / 1000.0f) * 48));

	song->m_SongTiming.m_fBeat0OffsetInSeconds = tempOffset;

	return row;
	// wow
	// this is fucking hideous
	// but it works
}

void
OsuLoader::LoadNoteDataFromParsedData(
  Steps* out,
  map<string, map<string, string>> parsedData)
{
	NoteData newNoteData;
	newNoteData.SetNumTracks(stoi(parsedData["Difficulty"]["CircleSize"]));

	auto it = parsedData["HitObjects"].begin();
	vector<OsuNote> taps;
	vector<OsuHold> holds;
	while (++it != parsedData["HitObjects"].end()) {
		auto line = it->first;
		auto values = split(line, ",");
		int type = stoi(values[3]);
		if (type == 128)
			holds.emplace_back(
			  OsuHold(stoi(values[2]), stoi(values[5]), stoi(values[0])));
		else if ((type & 1) == 1)
			taps.emplace_back(OsuNote(stoi(values[2]), stoi(values[0])));
	}

	sort(taps.begin(), taps.end(), [](OsuNote a, OsuNote b) {
		return a.ms < b.ms;
	});
	sort(holds.begin(), holds.end(), [](OsuHold a, OsuHold b) {
		return a.msStart < b.msStart;
	});

	int firstTap = 0;
	int lastTap = 0;
	if (taps.size() > 0 && holds.size() > 0) {
		firstTap = min(taps[0].ms, holds[0].msStart);
		lastTap = max(taps[taps.size()].ms, holds[holds.size()].msEnd);
	} else if (taps.size() > 0) {
		firstTap = taps[0].ms;
		lastTap = taps[taps.size()].ms;
	} else {
		firstTap = holds[0].msStart;
		lastTap = holds[holds.size()].msEnd;
	}

	for (int i = 0; i < (int)taps.size(); ++i) {
		newNoteData.SetTapNote(
		  taps[i].lane / (512 / stoi(parsedData["Difficulty"]["CircleSize"])),
		  MsToNoteRow(taps[i].ms - firstTap, out->m_pSong),
		  TAP_ORIGINAL_TAP);
	}
	for (int i = 0; i < (int)holds.size(); ++i) {
		int start = MsToNoteRow(holds[i].msStart - firstTap, out->m_pSong);
		int end = MsToNoteRow(holds[i].msEnd - firstTap, out->m_pSong);
		if (end - start > 0) {
			end = end - 1;
		}
		newNoteData.AddHoldNote(
		  holds[i].lane / (512 / stoi(parsedData["Difficulty"]["CircleSize"])),
		  start,
		  end,
		  TAP_ORIGINAL_HOLD_HEAD);
		newNoteData.SetTapNote(
		  holds[i].lane / (512 / stoi(parsedData["Difficulty"]["CircleSize"])),
		  end + 1,
		  TAP_ORIGINAL_LIFT);
	}

	// out->m_pSong->m_fMusicLengthSeconds = 80; // what's going on with this
	out->m_pSong->m_SongTiming.m_fBeat0OffsetInSeconds = -firstTap / 1000.0f;

	out->SetNoteData(newNoteData);
}

bool
OsuLoader::LoadNoteDataFromSimfile(const RString& path, Steps& out)
{
	RageFile f;
	if (!f.Open(path)) {
		LOG->UserLog(
		  "Song file", path, "couldn't be opened: %s", f.GetError().c_str());
		return false;
	}

	RString fileRStr;
	fileRStr.reserve(f.GetFileSize());
	f.Read(fileRStr, -1);

	string fileStr = fileRStr.c_str();
	auto parsedData = ParseFileString(fileStr.c_str());
	LoadNoteDataFromParsedData(&out, parsedData);

	return false;
}

bool
OsuLoader::LoadFromDir(const RString& sPath_, Song& out)
{
	vector<RString> aFileNames;
	GetApplicableFiles(sPath_, aFileNames);

	// const RString sPath = sPath_ + aFileNames[0];

	// LOG->Trace("Song::LoadFromDWIFile(%s)", sPath.c_str()); //osu

	RageFile f;
	map<string, map<string, string>> parsedData;

	for (auto& filename : aFileNames) {
		auto p = sPath_ + filename;

		if (!f.Open(p)) {
			continue;
		}
		RString fileContents;
		f.Read(fileContents, -1);
		parsedData = ParseFileString(fileContents.c_str());
		if (parsedData.size() == 0) {
			continue;
		}
		if (filename == aFileNames[0]) {
			SetMetadata(parsedData, out);
			SetTimingData(parsedData, out);
		}
		auto chart = out.CreateSteps();
		chart->SetFilename(p);
		if (!LoadChartData(&out, chart, parsedData)) {
			SAFE_DELETE(chart);
			continue;
		}

		LoadNoteDataFromParsedData(chart, parsedData);
		out.AddSteps(chart);
	}

	// out.Save(false);

	return true;
}
