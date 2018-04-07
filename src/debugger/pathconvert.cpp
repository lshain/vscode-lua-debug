#include <debugger/pathconvert.h>
#include <debugger/impl.h>
#include <base/util/unicode.h>
#include <base/util/dynarray.h>
#include <deque>
#include <Windows.h>

namespace vscode
{
	static bool is_sep(char c)
	{
		return c == '/' || c == '\\';
	}

	static void path_push(std::deque<std::string>& stack, const std::string& path)
	{
		if (path.empty()) {
			// do nothing
		}
		else if (path == ".." && !stack.empty() && stack.back() != "..") {
			stack.pop_back();
		}
		else if (path != ".") {
			stack.push_back(path);
		}
	}

	static std::string path_currentpath()
	{
		DWORD len = GetCurrentDirectoryW(0, nullptr);
		if (len == 0) {
			return std::string("c:");
		}
		std::wstring r;
		r.resize(len); 
		GetCurrentDirectoryW((DWORD)r.size(), &r.front());
		return base::w2u(r);
	}

	static std::string path_normalize(const std::string& path, std::deque<std::string>& stack)
	{
		std::string root;
		size_t pos = path.find(':', 0);
		if (pos == path.npos) {
			root = path_normalize(path_currentpath(), stack);
			pos = 0;
		}
		else {
			pos++;
			root = path.substr(0, pos);
		}

		for (size_t i = pos; i < path.size(); ++i) {
			char c = path[i];
			if (is_sep(c)) {
				if (i > pos) {
					path_push(stack, path.substr(pos, i - pos));
				}
				pos = i + 1;
			}
		}
		path_push(stack, path.substr(pos));
		return root;
	}

	std::string path_normalize(const std::string& path)
	{
		std::deque<std::string> stack;
		std::string result = path_normalize(path, stack);
		for (auto& e : stack) {
			result += '\\' + e;
		}
		return result;
	}

	std::string path_filename(const std::string& path)
	{
		for (ptrdiff_t pos = path.size() - 1; pos >= 0; --pos) {
			char c = path[pos];
			if (is_sep(c)) {
				return path.substr(pos + 1);
			}
		}
		return path;
	}

	pathconvert::pathconvert(debugger_impl* dbg)
		: debugger_(dbg)
		, sourcemaps_()
		, coding_(coding::ansi)
	{ }

	void pathconvert::set_coding(coding coding)
	{
		coding_ = coding;
		source2clientpath_.clear();
	}

	void pathconvert::add_sourcemap(const std::string& srv, const std::string& cli)
	{
		sourcemaps_.push_back(std::make_pair(srv, cli));
	}

	void pathconvert::clear_sourcemap()
	{
		sourcemaps_.clear();
	}

	bool pathconvert::match_sourcemap(const std::string& srv, std::string& cli, const std::string& srvmatch, const std::string& climatch)
	{
		size_t i = 0;
		for (; i < srvmatch.size(); ++i) {
			if (i >= srv.size()) {
				return false;
			}
			if (is_sep(srvmatch[i]) && is_sep(srv[i])) {
				continue;
			}
			if (tolower((int)(unsigned char)srvmatch[i]) == tolower((int)(unsigned char)srv[i])) {
				continue;
			}
			return false;
		}
		cli = climatch + srv.substr(i);
		return true;
	}

	std::string pathconvert::find_sourcemap(const std::string& srv)
	{
		std::string cli;
		for (auto& it : sourcemaps_)
		{
			if (match_sourcemap(srv, cli, it.first, it.second))
			{
				return cli;
			}
		}
		return path_normalize(srv);
	}

	bool pathconvert::get(const std::string& source, std::string& client_path)
	{
		auto it = source2clientpath_.find(source);
		if (it != source2clientpath_.end())
		{
			client_path = it->second;
			return true;
		}

		bool res = true;
		if (debugger_->custom_) {
			std::string path;
			if (debugger_->custom_->path_convert(source, path)) {
				client_path = find_sourcemap(path);
			}
			else {
				client_path.clear();
				res = false;
			}
		}
		else {
			if (source[0] == '@') {
				if (coding_ == coding::utf8) {
					client_path = find_sourcemap(source.substr(1));
				}
				else {
					client_path = find_sourcemap(base::a2u(base::strview(source.data() + 1, source.size() - 1)));
				}
			}
			else {
				client_path.clear();
				res = false;
			}
		}
		source2clientpath_[source] = client_path;
		return res;
	}
}