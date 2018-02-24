/*
    This file is part of Helio Workstation.

    Helio is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Helio is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Helio. If not, see <http://www.gnu.org/licenses/>.
*/

#include "Common.h"
#include "LegacySerializer.h"
#include "DocumentHelpers.h"

static const std::string kBase64Chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

static const char *kHelioHeaderV1String = "PR::";
static const int kHelioHeaderV1 = static_cast<int>(ByteOrder::littleEndianInt(kHelioHeaderV1String));

static const std::string kXorKey =
    "2V:-5?Vl%ulG+4-PG0`#:;[DUnB.Qs::"
    "v<{#]_oaa3NWyGtA[bq>Qf<i,28gV,,;"
    "y;W6rzn)ij}Ol%Eaxoq),+tx>l|@BS($"
    "7W9b9|46Fr&%pS!}[>5g5lly|bC]3aQu";

static inline std::string encodeBase64(unsigned char const *bytes_to_encode, size_t in_len)
{
    std::string ret;
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    while (in_len--)
    {
        char_array_3[i++] = *(bytes_to_encode++);

        if (i == 3)
        {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; (i < 4); i++)
            {
                ret += kBase64Chars[char_array_4[i]];
            }

            i = 0;
        }
    }

    if (i)
    {
        for (j = i; j < 3; j++)
        {
            char_array_3[j] = '\0';
        }

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for (j = 0; (j < i + 1); j++)
        {
            ret += kBase64Chars[char_array_4[j]];
        }

        while ((i++ < 3))
        {
            ret += '=';
        }
    }

    return ret;
}

static inline std::string encodeBase64(const std::string &s)
{
    return encodeBase64(reinterpret_cast<const unsigned char *>(s.data()), s.length());
}

static inline bool isBase64(unsigned char c)
{
    return (isalnum(c) || (c == '+') || (c == '/'));
}

static inline std::string decodeBase64(const std::string &encoded_string)
{
    size_t in_len = encoded_string.size();
    int i = 0;
    int j = 0;
    int in_ = 0;
    unsigned char char_array_4[4], char_array_3[3];
    std::string ret;

    while (in_len-- && (encoded_string[in_] != '=') && isBase64(encoded_string[in_]))
    {
        char_array_4[i++] = encoded_string[in_];
        in_++;

        if (i == 4)
        {
            for (i = 0; i < 4; i++)
            {
                char_array_4[i] = kBase64Chars.find(char_array_4[i]);
            }

            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

            for (i = 0; (i < 3); i++)
            {
                ret += char_array_3[i];
            }

            i = 0;
        }
    }

    if (i)
    {
        for (j = i; j < 4; j++)
        {
            char_array_4[j] = 0;
        }

        for (j = 0; j < 4; j++)
        {
            char_array_4[j] = kBase64Chars.find(char_array_4[j]);
        }

        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

        for (j = 0; (j < i - 1); j++) { ret += char_array_3[j]; }
    }

    return ret;
}

static inline MemoryBlock doXor(const MemoryBlock &input)
{
    MemoryBlock encoded;

    for (unsigned long i = 0; i < input.getSize(); i++)
    {
        char orig = input[i];
        char key = kXorKey[i % kXorKey.length()];
        char xorChar = orig ^ key;
        encoded.append(&xorChar, 1);
    }

    return encoded;
}

static inline MemoryBlock compress(const String &str)
{
    MemoryOutputStream memOut;
    GZIPCompressorOutputStream compressMemOut(&memOut, 1, false);
    compressMemOut.write(str.toRawUTF8(), str.getNumBytesAsUTF8());
    compressMemOut.flush();
    return MemoryBlock(memOut.getData(), memOut.getDataSize());
}

static inline String decompress(const MemoryBlock &str)
{
    MemoryInputStream input(str.getData(), str.getSize(), false);
    GZIPDecompressorInputStream gzInput(input);
    MemoryBlock decompressedData(0, true);
    MemoryBlock buf(512);

    while (!gzInput.isExhausted())
    {
        const int data = gzInput.read(buf.getData(), 512);
        decompressedData.append(buf.getData(), data);
    }

    return decompressedData.toString();
}

static String obfuscateString(const String &buffer)
{
    const MemoryBlock &compressed = compress(buffer);
    const MemoryBlock &xorBlock = doXor(compressed);
    const std::string &encoded = encodeBase64(reinterpret_cast<unsigned char const *>(xorBlock.getData()), xorBlock.getSize());
    const String &result = String::createStringFromData(encoded.data(), int(encoded.size()));
    return result;
}

static String deobfuscateString(const String &buffer)
{
    const std::string &decoded = decodeBase64(buffer.toStdString());
    const MemoryBlock &decodedMemblock = MemoryBlock(decoded.data(), decoded.size());
    const MemoryBlock &xorBlock = doXor(decodedMemblock);
    const String &uncompressed = decompress(xorBlock);
    return uncompressed;
}

static bool saveObfuscated(const File &file, XmlElement *xml)
{
    // Writes as plain text for debugging purposes:
    //#if defined _DEBUG
    //    
    //    const String xmlString(xml->createDocument("", false, true, "UTF-8", 512));
    //    const String xmlEncoded = xmlString;
    //    return file.replaceWithText(xmlEncoded);
    //    
    //#else

    const String xmlString(xml->createDocument("", false, true, "UTF-8", 512));
    const MemoryBlock &compressed = compress(xmlString);
    const MemoryBlock &xorBlock = doXor(compressed);

    MemoryInputStream obfuscatedStream(xorBlock, false);

    if (!file.existsAsFile())
    {
        Result creationResult = file.create();
        //Logger::writeToLog("Creating: " + file.getFullPathName() + ", result: " + creationResult.getErrorMessage());
    }

    DocumentHelpers::TempDocument tempFile(file);
    ScopedPointer <OutputStream> out(tempFile.getFile().createOutputStream());

    //Logger::writeToLog("Temp file: " + tempFile.getFile().getFullPathName());

    if (out != nullptr)
    {
        out->writeInt(kHelioHeaderV1);
        out->writeFromInputStream(obfuscatedStream, -1);
        out->flush();

        if (tempFile.overwriteTargetFileWithTemporary())
        {
            return true;
        }

        Logger::writeToLog("DocumentReader::saveObfuscated failed overwriteTargetFileWithTemporary");
    }
    else
    {
        Logger::writeToLog("DocumentReader::saveObfuscated failed");
    }

    return false;

    //#endif
}

static XmlElement *loadObfuscated(const File &file)
{
    //#if defined _DEBUG
    //    
    //    const String result = file.loadFileAsString();
    //    const String decodedResult = result;
    //    
    //    XmlElement *xml = XmlDocument::parse(decodedResult);
    //    return tree;
    //    
    //#else

    FileInputStream fileStream(file);

    if (fileStream.openedOk())
    {
        const int magicNumber = fileStream.readInt();
        
        if (magicNumber == kHelioHeaderV1)
        {
            SubregionStream subStream(&fileStream, 4, -1, false);
            MemoryBlock subBlock;
            subStream.readIntoMemoryBlock(subBlock);
            const MemoryBlock &xorBlock = doXor(subBlock);
            const String &uncompressed = decompress(xorBlock);
            XmlElement *xml = XmlDocument::parse(uncompressed);
            return xml;
        }
    }

    return nullptr;

    //#endif
}

Result LegacySerializer::saveToFile(File file, const ValueTree &tree) const
{
    const auto saved = file.replaceWithText(tree.toXmlString());
    return saved ? Result::ok() : Result::fail({});
}

Result LegacySerializer::loadFromFile(const File &file, ValueTree &tree) const
{
    XmlDocument document(file);
    ScopedPointer<XmlElement> xml(document.getDocumentElement());
    tree = ValueTree::fromXml(*xml);
    return Result::ok();
}

Result LegacySerializer::saveToString(String &string, const ValueTree &tree) const
{
    string = tree.toXmlString();
    return Result::ok();
}

Result LegacySerializer::loadFromString(const String &string, ValueTree &tree) const
{
    XmlDocument document(string);
    ScopedPointer<XmlElement> xml(document.getDocumentElement());
    tree = ValueTree::fromXml(*xml);
    return Result::ok();
}

bool LegacySerializer::supportsFileWithExtension(const String &extension) const
{
    return extension.endsWithIgnoreCase("hp") ||
        extension.endsWithIgnoreCase("helio") ||
        extension.endsWithIgnoreCase("pack");
}

bool LegacySerializer::supportsFileWithHeader(const String &header) const
{
    return header.startsWith(kHelioHeaderV1String);
}