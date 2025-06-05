#define NOMINMAX  // Отключаем min/max макросы Windows
#include <iostream>
#include <string>
#include <map>
#include <iomanip>
#include <cctype>
#include <limits>
#include <sstream>
#include <vector>
#include <windows.h>
#include <winhttp.h>
#include "json.hpp"  // Убедитесь, что файл в папке проекта!

#pragma comment(lib, "winhttp.lib")

using namespace std;
using json = nlohmann::json;

// Функция для выполнения HTTP-запросов
string http_get(const string& url) {
    string result;
    HINTERNET hSession = NULL;
    HINTERNET hConnect = NULL;
    HINTERNET hRequest = NULL;

    // Разбор URL
    URL_COMPONENTS urlComp = { 0 };
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwHostNameLength = -1;
    urlComp.dwUrlPathLength = -1;
    urlComp.dwExtraInfoLength = -1;

    wstring wide_url(url.begin(), url.end());
    if (!WinHttpCrackUrl(wide_url.c_str(), static_cast<DWORD>(wide_url.size()), 0, &urlComp)) {
        cerr << "URL parsing error:" << GetLastError() << endl;
        return "";
    }

    // Извлечение компонентов URL
    wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
    wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
    if (urlComp.dwExtraInfoLength) {
        path += wstring(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
    }

    // Открытие сессии
    hSession = WinHttpOpen(L"CurrencyConverter/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        cerr << "Error WinHttpOpen: " << GetLastError() << endl;
        return "";
    }

    // Подключение к серверу
    hConnect = WinHttpConnect(hSession, host.c_str(),
        urlComp.nPort, 0);
    if (!hConnect) {
        cerr << "Error WinHttpConnect: " << GetLastError() << endl;
        WinHttpCloseHandle(hSession);
        return "";
    }

    // Создание запроса
    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ?
        WINHTTP_FLAG_SECURE : 0;
    hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
        NULL, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags);
    if (!hRequest) {
        cerr << "Error WinHttpOpenRequest: " << GetLastError() << endl;
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }

    // Отправка запроса
    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        cerr << "Error WinHttpSendRequest: " << GetLastError() << endl;
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }

    // Получение ответа
    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        cerr << "Error WinHttpReceiveResponse: " << GetLastError() << endl;
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }

    // Чтение данных ответа
    DWORD dwSize = 0;
    DWORD dwDownloaded = 0;
    vector<char> buffer;
    do {
        // Проверка доступных данных
        if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) {
            cerr << "Error WinHttpQueryDataAvailable: " << GetLastError() << endl;
            break;
        }

        if (dwSize == 0) break;

        // Чтение данных
        buffer.resize(dwSize + 1);
        if (!WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) {
            cerr << "WinHttpReadData Error:" << GetLastError() << endl;
            break;
        }

        // Добавление данных в результат
        if (dwDownloaded > 0) {
            result.append(buffer.data(), dwDownloaded);
        }
    } while (dwSize > 0);

    // Закрытие дескрипторов
    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);

    return result;
}

class CurrencyConverter {
public:
    CurrencyConverter(const string& api_key) : api_key_(api_key) {}

    bool load_currency_codes() {
        string url = "https://v6.exchangerate-api.com/v6/" + api_key_ + "/codes";
        string response = http_get(url);

        if (response.empty()) {
            cerr << "Empty response from API" << endl;
            return false;
        }

        try {
            json data = json::parse(response);
            for (const auto& item : data["supported_codes"]) {
                currencies_[item[0].get<string>()] = item[1].get<string>();
            }
            return true;
        }
        catch (const exception& e) {
            cerr << "JSON parsing error:" << e.what() << endl;
            return false;
        }
    }

    double convert(double amount, const string& from, const string& to) {
        string url = "https://v6.exchangerate-api.com/v6/" + api_key_ + "/pair/" + from + "/" + to + "/" + to_string(amount);
        string response = http_get(url);

        if (response.empty()) {
            cerr << "Empty response from API" << endl;
            return -1.0;
        }

        try {
            json data = json::parse(response);
            return data["conversion_result"].get<double>();
        }
        catch (const exception& e) {
            cerr << "JSON parsing error:" << e.what() << endl;
            return -1.0;
        }
    }

    const map<string, string>& get_currencies() const {
        return currencies_;
    }

private:
    string api_key_;
    map<string, string> currencies_;
};

void print_menu() {
    cout << "\n=== Currency converter ===" << endl;
    cout << "1. Convert currency" << endl;
    cout << "2. Show list of currencies" << endl;
    cout << "3. Exit" << endl;
    cout << "Select an option:";
}

void print_currencies(const map<string, string>& currencies) {
    cout << "\nAvailable currencies:" << endl;
    cout << "==============================================" << endl;
    for (const auto& currency : currencies) {
        cout << setw(5) << currency.first << " - " << currency.second << endl;
    }
    cout << "==============================================" << endl;
}

string input_currency_code(const string& prompt, const map<string, string>& currencies) {
    string code;
    while (true) {
        cout << prompt;
        cin >> code;

        // Конвертируем в верхний регистр
        for (char& c : code) c = toupper(c);

        if (currencies.find(code) != currencies.end()) {
            return code;
        }
        cout << "Unknown currency code. Try again." << endl;
    }
}

int main() {
    const string api_key = "54e7ddf15f36d2f0bf524747";
    CurrencyConverter converter(api_key);

    if (!converter.load_currency_codes()) {
        cerr << "Error loading currency list. Check your API key or connection." << endl;
        return 1;
    }

    int choice;
    while (true) {
        print_menu();
        cin >> choice;

        if (cin.fail()) {
            cin.clear();
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            cout << "Incorrect input. Please enter a number." << endl;
            continue;
        }

        switch (choice) {
        case 1: {
            double amount;
            cout << "Enter amount:";
            while (!(cin >> amount) || amount <= 0) {
                cin.clear();
                cin.ignore(numeric_limits<streamsize>::max(), '\n');
                cout << "Incorrect amount. Please enter a positive number:";
            }

            auto& currencies = converter.get_currencies();
            string from = input_currency_code("Enter the source currency (example: USD):", currencies);
            string to = input_currency_code("Enter the target currency (example: EUR):", currencies);

            double result = converter.convert(amount, from, to);
            if (result < 0) {
                cout << "Conversion error. Check your input or connection." << endl;
            }
            else {
                cout << fixed << setprecision(2);
                cout << amount << " " << from << " = " << result << " " << to << endl;
            }
            break;
        }
        case 2:
            print_currencies(converter.get_currencies());
            break;
        case 3:
            cout << "Exit the program." << endl;
            return 0;
        default:
            cout << "Unknown option. Please try again." << endl;
        }
    }
}