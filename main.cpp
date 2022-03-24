#include <iostream>
#include <string>
#include <vector>
#include "filesystem"
#include <codecvt>
#include <sstream>
#include <fstream>

#include "dictionary.h"
#include "filemap.h"
#include "WordContext.h"

using namespace std;
using recursive_directory_iterator = std::__fs::filesystem::recursive_directory_iterator;

int CONTEXT_FREQUENCY_THRESHOLD = 5;

vector<string> getFilesFromDir(const string& dirPath) {
    vector<string> files;
    for (const auto& dirEntry : recursive_directory_iterator(dirPath))
        if (!(dirEntry.path().filename() == ".DS_Store"))
            files.push_back(dirEntry.path());
    return files;
}

vector<WordContext> makeAllContexts(vector<vector<Word*>> wordVectors, wstring &stringContext) {
    vector<vector<Word*>> allWordContexts;

    for (int i = 0; i < wordVectors.size(); i++) {
        auto& currentVector = wordVectors.at(i);
        if (i == 0) {
            set<wstring> handledForms;
            for (Word *word: currentVector)
                if (handledForms.find(word->word) == handledForms.end()) {
                    handledForms.insert(word->word);
                    allWordContexts.push_back(vector<Word *>{word});
                }
        }
        else if (currentVector.size() == 1) {
            for (vector<Word*>& context : allWordContexts)
                context.push_back(currentVector.at(0));
        } else {
            vector<vector<Word*>> newAllContexts;
            set<wstring> handledForms;
            for (Word *word: currentVector)
                for (const vector<Word*>& context : allWordContexts) {
                    if (handledForms.find(word->word) == handledForms.end()) {
                        handledForms.insert(word->word);
                        vector<Word*> newContext;
                        newContext = context;
                        newContext.push_back(word);
                        newAllContexts.push_back(newContext);
                    }
                }
            allWordContexts = newAllContexts;
        }
    }

    vector<WordContext> allContexts;

    for (const auto& contextVector : allWordContexts) {
        wstring normalizedForm;
        for (Word* word : contextVector)
            normalizedForm += word->word + L" ";

        normalizedForm.pop_back();
        allContexts.emplace_back(stringContext, normalizedForm,contextVector);
    }
    return allContexts;
}

vector<WordContext> processContext(const vector<wstring>& stringContext, unordered_map <wstring, vector<Word*>> *dictionary) {
    auto& f = std::use_facet<std::ctype<wchar_t>>(std::locale());

    wstring rawContext;
    vector<vector<Word*>> contextVector;

    for (wstring contextWord: stringContext) {
        rawContext += contextWord + L" ";
        f.toupper(&contextWord[0], &contextWord[0] + contextWord.size());

        if (dictionary->find(contextWord) == dictionary->end()) {
            Word *newWord = new Word();
            newWord->word = contextWord;
            newWord->partOfSpeech = L"UNKW";
            dictionary->emplace(newWord->word, vector<Word*>{newWord});
        }

        vector<Word*> words = dictionary->at(contextWord);
        contextVector.push_back(words);
    }

    rawContext.pop_back();

    return makeAllContexts(contextVector, rawContext);
}

bool haveCommonForm(wstring firstWord, wstring secondWord, unordered_map <wstring, vector<Word*>> *dictionary) {
    auto& f = std::use_facet<std::ctype<wchar_t>>(std::locale());
    f.toupper(&firstWord[0], &firstWord[0] + firstWord.size());
    f.toupper(&secondWord[0], &secondWord[0] + secondWord.size());
    vector<Word*> words1;
    vector<Word*> words2;

    if (dictionary->find(firstWord) == dictionary->end()) {
        Word *newWord = new Word();
        newWord->word = firstWord;
        newWord->partOfSpeech = L"UNKW";
        dictionary->emplace(newWord->word, vector<Word*>{newWord});
    }

    if (dictionary->find(secondWord) == dictionary->end()) {
        Word *newWord = new Word();
        newWord->word = secondWord;
        newWord->partOfSpeech = L"UNKW";
        dictionary->emplace(newWord->word, vector<Word*>{newWord});
    }

    words1 = dictionary->at(firstWord);
    words2 = dictionary->at(secondWord);

    for (Word* word1: words1) {
        for (Word* word2: words2) {
            if (word1->word == word2->word)
                return true;
        }
    }

    return false;
}

void addContextsToSet(vector<WordContext>& contexts, unordered_map <wstring, WordContext>* concordances) {
    for (WordContext context : contexts) {
        if (concordances->find(context.normalizedForm) == concordances->end())
            concordances->emplace(context.normalizedForm, context);

        concordances->at(context.normalizedForm).count++;
    }
}

void handleFile(const string& filePath, unordered_map <wstring, vector<Word*>> *dictionary,
               vector<wstring> target, int windowSize,
                unordered_map <wstring, WordContext> *leftStringToContext,
                unordered_map <wstring, WordContext> *rightStringToContext) {

    size_t length;
    auto filePtr = map_file(filePath.c_str(), length);
    auto lastChar = filePtr + length;

    vector<wstring> leftStringConcordance;
    vector<wstring> rightStringConcordance;

    int currentTargetPos = 0;

    while (filePtr && filePtr != lastChar) {
        auto stringBegin = filePtr;
        filePtr = static_cast<char *>(memchr(filePtr, '\n', lastChar - filePtr));

        wstring line = wstring_convert<codecvt_utf8<wchar_t>>().from_bytes(stringBegin, filePtr);

        int begin = -1;
        for (int pos = 0; pos < line.size(); pos++) {
            wchar_t symbol = line[pos];
            if (iswcntrl(symbol) || iswpunct(symbol) || iswspace(symbol)
                || iswdigit(symbol) || iswcntrl(symbol)) {
                if (begin != -1) {
                    wstring wordStr = line.substr(begin, pos - begin);
                    begin = -1;

                    if (!wordStr.empty()) {
                        if (rightStringConcordance.size() == windowSize + target.size()) {
                            vector<WordContext> phraseContexts = processContext(rightStringConcordance, dictionary);
                            addContextsToSet(phraseContexts, rightStringToContext);
                            rightStringConcordance.clear();
                        } else if (rightStringConcordance.size() >= target.size()) {
                            rightStringConcordance.push_back(wordStr);
                        }

                        if (haveCommonForm(wordStr, target[currentTargetPos], dictionary)) {
                            rightStringConcordance.push_back(wordStr);

                            if (currentTargetPos == target.size() - 1) {
                                leftStringConcordance.push_back(wordStr);
                                vector<WordContext> phraseContexts = processContext(leftStringConcordance, dictionary);
                                addContextsToSet(phraseContexts, leftStringToContext);
                                currentTargetPos = 0;
                                leftStringConcordance.pop_back();
                            } else
                                currentTargetPos++;
                        } else if (currentTargetPos > 0) {
                            currentTargetPos = 0;
                            rightStringConcordance.clear();
                        }

                        while (leftStringConcordance.size() >= windowSize + target.size() - 1)
                            leftStringConcordance.erase(leftStringConcordance.begin());

                        leftStringConcordance.push_back(wordStr);
                    }
                }
            } else if (begin == -1) {
                begin = pos;
            }
        }
        if (filePtr)
            filePtr++;
    }
}

vector<WordContext> findConcordances(const string& dictPath, const string& corpusPath,
                                     const wstring& targetString, int windowSize, string outputFile) {
    vector<wstring> targetVector;

    wistringstream iss(targetString);
    wstring s;
    while (getline(iss,s,L' '))
        targetVector.push_back(s);

    auto dictionary = initDictionary(dictPath);
    vector<string> files = getFilesFromDir(corpusPath);

    vector<WordContext> concordances;

    unordered_map <wstring, WordContext> leftStringToContext;
    unordered_map <wstring, WordContext> rightStringToContext;

    cout << "Start handling files\n";

    for (const string& file : files)
        handleFile(file, &dictionary,
                   targetVector, windowSize,
                   &leftStringToContext, &rightStringToContext);

    vector<WordContext> leftContexts;
    vector<WordContext> rightContexts;
    leftContexts.reserve(leftStringToContext.size());
    rightContexts.reserve(rightStringToContext.size());

    for(const auto& pair : leftStringToContext)
        leftContexts.push_back(pair.second);

    for(const auto& pair : rightStringToContext)
        rightContexts.push_back(pair.second);

    struct {
        bool operator()(const WordContext& a, const WordContext& b) const { return a.count > b.count; }
    } comp;

    sort(leftContexts.begin(), leftContexts.end(), comp);
    sort(rightContexts.begin(), rightContexts.end(), comp);

    ofstream outFile (outputFile.c_str());

    int printCount = 0;
    cout << "\n";
    for (const WordContext& context : leftContexts) {
        if (context.count >= CONTEXT_FREQUENCY_THRESHOLD) {
            wcout << "<Left, \"" << context.rawValue << "\", " << context.count << ">\n";
            printCount++;
        }

        string line = wstring_convert<codecvt_utf8<wchar_t>>().to_bytes(context.rawValue);
        outFile << "<Left, \"" << line << "\", " << context.count << ">\n";
    }

    printCount = 0;
    cout << "\n";
    for (const WordContext& context : rightContexts) {
        if (context.count >= CONTEXT_FREQUENCY_THRESHOLD) {
            wcout << "<Right, \"" << context.rawValue << "\", " << context.count << ">\n";
            printCount++;
        }

        string line = wstring_convert<codecvt_utf8<wchar_t>>().to_bytes(context.rawValue);
        outFile << "<Right, \"" << line << "\", " << context.count << ">\n";
    }

    outFile.close();

    return concordances;
}

int main() {
    CONTEXT_FREQUENCY_THRESHOLD = 10;

    locale::global(locale("ru_RU.UTF-8"));
    wcout.imbue(locale("ru_RU.UTF-8"));

    string dictPath = "dict_opcorpora_clear.txt";
    string corpusPath = "/Users/titrom/Desktop/Computational Linguistics/Articles";
    const int windowSize = 1;
    wstring target = L"разработка";

    string outputFile = "concordances.txt";

    auto concordances = findConcordances(dictPath, corpusPath, target, windowSize, outputFile);
    return 0;
}
