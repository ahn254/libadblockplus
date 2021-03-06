#include <cmath>
#include <fstream>
#include <gtest/gtest.h>
#include <numeric>

#include "../src/DefaultFileSystem.h"
#include "../src/JsError.h"
#include "BaseJsTest.h"

class ReadOnlyFileSystem : public AdblockPlus::DefaultFileSystem
{
public:
  ReadOnlyFileSystem(AdblockPlus::IExecutor& executor, const std::string& basePath)
      : AdblockPlus::DefaultFileSystem(
            executor, std::make_unique<AdblockPlus::DefaultFileSystemSync>(basePath))
  {
  }

  void Write(const std::string& fileName, const IOBuffer& data, const Callback& callback) override
  {
    callback("");
  }

  void Move(const std::string& fromFileName,
            const std::string& toFileName,
            const Callback& callback) override
  {
    callback("");
  }

  void Remove(const std::string& fileName, const Callback& callback) override
  {
    callback("");
  }
};

enum class PopupBlockResult
{
  NO_RULE,
  BLOCK_RULE,
  ALLOW_RULE,
  DISABLED
};

class ElapsedTime
{
public:
  ElapsedTime()
  {
    start = std::chrono::steady_clock::now();
  }

  double Microseconds() const
  {
    std::chrono::steady_clock::duration d = std::chrono::steady_clock::now() - start;
    return std::chrono::duration_cast<std::chrono::microseconds>(d).count();
  }

private:
  std::chrono::steady_clock::time_point start;
};

struct CallStats
{
  void Add(double elapsedTime)
  {
    measurements.push_back(elapsedTime);
  }

  double Median()
  {
    const size_t size = measurements.size();
    if (size == 0)
      return 0;

    std::sort(measurements.begin(), measurements.end());
    return size % 2 == 0 ? (measurements[size / 2 - 1] + measurements[size / 2]) / 2
                         : measurements[size / 2];
  }

  double Mean()
  {
    const size_t size = measurements.size();
    if (size == 0)
      return 0;
    const double total = std::accumulate(measurements.begin(), measurements.end(), 0.0);
    return total / size;
  }

  double StdDeviation()
  {
    const size_t size = measurements.size();
    if (size < 2)
      return 0;

    const double mean = Mean();
    const double sumOfSquaredDeviations =
        std::accumulate(measurements.begin(), measurements.end(), 0, [mean](double sum, double b) {
          return (b - mean) * (b - mean) + sum;
        });
    return std::sqrt(sumOfSquaredDeviations / (size - 1));
  }

  double StdError()
  {
    const size_t size = measurements.size();
    if (size == 0)
      return 0;
    return StdDeviation() / std::sqrt(double(size));
  }

  std::vector<double> measurements;
};

class HarnessTest : public ::testing::Test
{
protected:
  std::unique_ptr<AdblockPlus::Platform> platform;
  std::map<std::string, CallStats> stats;

  void SetUp() override
  {
    AdblockPlus::AppInfo appInfo;
    appInfo.version = "1.0";
    appInfo.name = "abppplayer";
    appInfo.application = "standalone";
    appInfo.applicationVersion = "1.0";
    appInfo.locale = "en-US";

    AdblockPlus::PlatformFactory::CreationParameters params;
    params.executor = AdblockPlus::PlatformFactory::CreateExecutor();
    params.fileSystem.reset(new ReadOnlyFileSystem(*params.executor, "data"));
    params.webRequest.reset(new NoopWebRequest());

    AdblockPlus::FilterEngineFactory::CreationParameters engineParams;
    engineParams.preconfiguredPrefs.booleanPrefs
        [AdblockPlus::FilterEngineFactory::BooleanPrefName::FirstRunSubscriptionAutoselect] = false;

    platform = AdblockPlus::PlatformFactory::CreatePlatform(std::move(params));
    platform->SetUp(appInfo);
    platform->CreateFilterEngineAsync(engineParams);
  }

  AdblockPlus::JsEngine& GetJsEngine()
  {
    return static_cast<AdblockPlus::DefaultPlatform*>(platform.get())->GetJsEngine();
  }

  AdblockPlus::IFilterEngine& GetFilterEngine() const
  {
    return platform->GetFilterEngine();
  }

  void MatchFromFile(const std::string& file)
  {
    std::ifstream stream(file);
    std::string line;
    ASSERT_TRUE(stream.is_open());

    while (std::getline(stream, line))
      if (!line.empty())
        MatchRecorded(line);
  }

  void MatchRecorded(const std::string& json)
  {
    auto& engine = GetJsEngine();
    AdblockPlus::JsValue callInfo =
        engine.Evaluate("str => JSON.parse(str)").Call(engine.NewValue(json));

    std::string fn = callInfo.GetProperty("_fn").AsString();

    if (fn == "check-filter-match")
      stats[fn].Add(CheckFilterMatch(callInfo));
    else if (fn == "block-popup")
      stats[fn].Add(BlockPopup(callInfo));
    else if (fn == "generate-js-css")
      stats[fn].Add(GenerateJsCss(callInfo));
  }

  std::vector<std::string> ToList(const AdblockPlus::JsValue& value) const
  {
    std::vector<std::string> res;

    for (const AdblockPlus::JsValue& it : value.AsList())
      res.push_back(it.AsString());

    return res;
  }

  double GenerateJsCss(const AdblockPlus::JsValue& info) const
  {
    auto& engine = GetFilterEngine();
    auto url = info.GetProperty("gurl").AsString();
    auto process_id = info.GetProperty("process_id").AsInt();
    auto frame_id = info.GetProperty("frame_id").AsInt();
    auto documentUrls = ToList(info.GetProperty("referrers"));
    auto sitekey = info.GetProperty("sitekey").AsString();
    double lasted = 0;

    {
      ElapsedTime timer;

      if (url.rfind("http:", 0) == 0 || url.rfind("https:", 0) == 0)
      {
        if (!engine.IsContentAllowlisted(
                url, AdblockPlus::IFilterEngine::CONTENT_TYPE_DOCUMENT, documentUrls, sitekey) &&
            !engine.IsContentAllowlisted(
                url, AdblockPlus::IFilterEngine::CONTENT_TYPE_ELEMHIDE, documentUrls, sitekey))
        {
          if (process_id >= 0 && frame_id >= 0)
          {
            engine.GetElementHidingEmulationSelectors(url);
            engine.GetElementHidingStyleSheet(
                url,
                engine.IsContentAllowlisted(
                    url, AdblockPlus::IFilterEngine::CONTENT_TYPE_GENERICHIDE, documentUrls));
          }
        }
      }

      lasted = timer.Microseconds();
    }

    return lasted;
  }

  double BlockPopup(const AdblockPlus::JsValue& info) const
  {
    auto& engine = GetFilterEngine();
    auto url = info.GetProperty("url").AsString();
    auto opener = info.GetProperty("opener").AsString();
    double lasted = 0;

    {
      ElapsedTime timer;

      AdblockPlus::Filter filter =
          engine.Matches(url, AdblockPlus::IFilterEngine::ContentType::CONTENT_TYPE_POPUP, opener);
      lasted = timer.Microseconds();
    }

    EXPECT_EQ(info.GetProperty("_res").AsInt(), static_cast<int>(lasted));
    return lasted;
  }

  double CheckFilterMatch(const AdblockPlus::JsValue& info) const
  {
    auto& engine = GetFilterEngine();
    auto url = info.GetProperty("request_url").AsString();
    auto documentUrls = ToList(info.GetProperty("referrers"));
    auto sitekey = info.GetProperty("sitekey").AsString();
    AdblockPlus::IFilterEngine::ContentType contentTypeMask =
        static_cast<AdblockPlus::IFilterEngine::ContentType>(
            info.GetProperty("adblock_resource_type").AsInt());
    bool decision = false;
    double lasted = 0;

    {
      ElapsedTime timer;
      bool specificOnly = false;
      AdblockPlus::Filter filter;

      if (!documentUrls.empty())
      {
        specificOnly = engine.IsContentAllowlisted(
            url,
            AdblockPlus::IFilterEngine::ContentType::CONTENT_TYPE_GENERICBLOCK,
            documentUrls,
            sitekey);
      }

      if (documentUrls.empty())
        filter = engine.Matches(url, contentTypeMask, "", sitekey, specificOnly);
      else
        filter = engine.Matches(url, contentTypeMask, documentUrls.front(), sitekey, specificOnly);

      decision = filter.IsValid() && filter.GetType() != AdblockPlus::Filter::Type::TYPE_EXCEPTION;

      if (decision && engine.IsContentAllowlisted(
                          url,
                          AdblockPlus::IFilterEngine::ContentType::CONTENT_TYPE_DOCUMENT,
                          documentUrls,
                          sitekey))
      {
        decision = false;
      }

      lasted = timer.Microseconds();
    }

    EXPECT_EQ(info.GetProperty("_res").AsInt(), decision);
    return lasted;
  }

  void ReportPerformance()
  {
    std::cout << std::left << std::fixed << std::setprecision(3) << std::setw(20) << "Name"
              << " ; Median(us) ; StdDev(us) ; StdErr(us) ;      Count" << std::endl;

    for (auto& it : stats)
    {
      const std::string& name = it.first;
      CallStats& cbStats = it.second;
      std::cout << std::left << std::setw(20) << name << " ; " << std::right << std::setw(10)
                << cbStats.Median() << " ; " << std::setw(10) << cbStats.StdDeviation() << " ; "
                << std::setw(10) << cbStats.StdError() << " ; " << std::setw(10)
                << cbStats.measurements.size() << std::endl;
    }
  }
};

TEST_F(HarnessTest, AllSites)
{
  MatchFromFile("data/rec_abudhabi_dubizzle_com.log");
  MatchFromFile("data/rec_allegro_pl.log");
  MatchFromFile("data/rec_chron_com.log");
  MatchFromFile("data/rec_cn_hao123_com.log");
  MatchFromFile("data/rec_en_wikipedia_org.log");
  MatchFromFile("data/rec_laodong_vn.log");
  MatchFromFile("data/rec_news_mail_ru.log");
  MatchFromFile("data/rec_search_yahoo_com.log");
  MatchFromFile("data/rec_shopee_vn.log");
  MatchFromFile("data/rec_shortorial_com.log");
  MatchFromFile("data/rec_thethao247_vn.log");
  MatchFromFile("data/rec_vk_com.log");
  MatchFromFile("data/rec_vnexpress_net.log");
  MatchFromFile("data/rec_vtv_vn.log");
  MatchFromFile("data/rec_web_de.log");
  MatchFromFile("data/rec_www_1tv_ge.log");
  MatchFromFile("data/rec_www_24h_com_vn.log");
  MatchFromFile("data/rec_www_amazon_com.log");
  MatchFromFile("data/rec_www_aparat_com.log");
  MatchFromFile("data/rec_www_baidu_com.log");
  MatchFromFile("data/rec_www_bbc_com.log");
  MatchFromFile("data/rec_www_bedienungsanleitu_ng.log");
  MatchFromFile("data/rec_www_bing_com.log");
  MatchFromFile("data/rec_www_boston_com.log");
  MatchFromFile("data/rec_www_dailymail_co_uk.log");
  MatchFromFile("data/rec_www_ebay_com.log");
  MatchFromFile("data/rec_www_flipkart_com.log");
  MatchFromFile("data/rec_www_forbes_com.log");
  MatchFromFile("data/rec_www_google_com.log");
  MatchFromFile("data/rec_www_imdb_com.log");
  MatchFromFile("data/rec_www_indiatimes_com.log");
  MatchFromFile("data/rec_www_libero_it.log");
  MatchFromFile("data/rec_www_manoramaonline_com.log");
  MatchFromFile("data/rec_www_myauto_ge.log");
  MatchFromFile("data/rec_www_ndtv_com.log");
  MatchFromFile("data/rec_www_olx_ro.log");
  MatchFromFile("data/rec_www_online2pdf_com.log");
  MatchFromFile("data/rec_www_quora_com.log");
  MatchFromFile("data/rec_www_reddit_com.log");
  MatchFromFile("data/rec_www_repubblica_it.log");
  MatchFromFile("data/rec_www_sapo_pt.log");
  MatchFromFile("data/rec_www_techradar_com.log");
  MatchFromFile("data/rec_www_tomsguide_com.log");
  MatchFromFile("data/rec_www_trustedreviews_com.log");
  MatchFromFile("data/rec_www_twitch_tv.log");
  MatchFromFile("data/rec_www_wp_pl.log");
  MatchFromFile("data/rec_www_xvideos_com.log");
  MatchFromFile("data/rec_www_youtube_com.log");
  MatchFromFile("data/rec_yandex_com.log");

  ReportPerformance();
}
