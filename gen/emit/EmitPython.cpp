#include "Common.hpp"
#include "Emitter.hpp"
#include "GetOpt.hpp"
#include "ZCMGen.hpp"
#include "util/StringUtil.hpp"
#include "util/FileUtil.hpp"
using std::to_string;

#include <queue>
using std::queue;

#include <iostream>
using std::cerr;

void setupOptionsPython(GetOpt& gopt)
{
    gopt.addString(0, "ppath", "", "Python destination directory");
}

static char getStructFormat(ZCMMember& lm)
{
    auto& tn = lm.type.fullname;
    if (tn == "byte")    return 'B';
    if (tn == "boolean") return 'b';
    if (tn == "int8_t")  return 'b';
    if (tn == "int16_t") return 'h';
    if (tn == "int32_t") return 'i';
    if (tn == "int64_t") return 'q';
    if (tn == "float")   return 'f';
    if (tn == "double")  return 'd';
    return 0;
}

static int getPrimitiveTypeSize(const string& tn)
{
    if (tn == "byte")    return 1;
    if (tn == "boolean") return 1;
    if (tn == "int8_t")  return 1;
    if (tn == "int16_t") return 2;
    if (tn == "int32_t") return 4;
    if (tn == "int64_t") return 8;
    if (tn == "float")   return 4;
    if (tn == "double")  return 8;
    assert(0);
    return 0;
}

struct PyEmitStruct : public Emitter
{
    ZCMGen& zcm;
    ZCMStruct& ls;

    PyEmitStruct(ZCMGen& zcm, ZCMStruct& ls, const string& fname):
        Emitter(fname), zcm(zcm), ls(ls) {}

    void emitStruct()
    {
        auto& sn_ = ls.structname.shortname;
        auto *sn = sn_.c_str();

        emit(0, "\"\"\"ZCM type definitions\n"
             "This file automatically generated by zcm.\n"
             "DO NOT MODIFY BY HAND!!!!\n"
             "\"\"\"\n"
             "\n"
             "try:\n"
             "    import cStringIO.StringIO as BytesIO\n"
             "except ImportError:\n"
             "    from io import BytesIO\n"
             "import struct\n");

        emitPythonDependencies();

        emit(0, "class %s(object):", sn);
        emitStart(0, "    __slots__ = [");
        for (uint m = 0; m < ls.members.size(); m++) {
            auto& lm = ls.members[m];
            emitContinue("\"%s\"%s", lm.membername.c_str(),
                         (m < ls.members.size()-1) ? ", " : "");
        }
        emitEnd("]");
        emit(0, "");

        // CONSTANTS
        for (auto& lc : ls.constants) {
            assert(ZCMGen::isLegalConstType(lc.type));
            emit(1, "%s = %s", lc.membername.c_str(), lc.valstr.c_str());
        }
        if (ls.constants.size() > 0)
            emit(0, "");

        emitPythonInit();
        emitPythonEncode();
        emitPythonEncodeOne();
        emitPythonDecode();
        emitPythonDecodeOne();
        emitPythonFingerprint();
    }

    void emitDecodeOne(ZCMMember& lm, const string& accessor_, int indent, const string& sfx_)
    {
        auto& tn = lm.type.fullname;
        auto& mn = lm.membername;
        auto& sn = lm.type.shortname;

        auto *accessor = accessor_.c_str();
        auto *sfx = sfx_.c_str();

        if (tn == "string") {
            emit(indent, "__%s_len = struct.unpack('>I', buf.read(4))[0]", mn.c_str());
            emit(indent, "%sbuf.read(__%s_len)[:-1].decode('utf-8', 'replace')%s",
                 accessor, mn.c_str(), sfx);
        } else if (tn == "byte") {
            emit(indent, "%sstruct.unpack('B', buf.read(1))[0]%s", accessor, sfx);
        } else if (tn == "boolean") {
            emit(indent, "%sbool(struct.unpack('b', buf.read(1))[0])%s", accessor, sfx);
        } else if (tn == "int8_t") {
            emit(indent, "%sstruct.unpack('b', buf.read(1))[0]%s", accessor, sfx);
        } else if (tn == "int16_t") {
            emit(indent, "%sstruct.unpack('>h', buf.read(2))[0]%s", accessor, sfx);
        } else if (tn == "int32_t") {
            emit(indent, "%sstruct.unpack('>i', buf.read(4))[0]%s", accessor, sfx);
        } else if (tn == "int64_t") {
            emit(indent, "%sstruct.unpack('>q', buf.read(8))[0]%s", accessor, sfx);
        } else if (tn == "float") {
            emit(indent, "%sstruct.unpack('>f', buf.read(4))[0]%s", accessor, sfx);
        } else if (tn == "double") {
            emit(indent, "%sstruct.unpack('>d', buf.read(8))[0]%s", accessor, sfx);
        } else {
            if (lm.type.fullname == ls.structname.fullname) {
                emit(indent, "%s%s._decode_one(buf)%s", accessor, sn.c_str(), sfx);
            } else {
                emit(indent, "%s%s._decode_one(buf)%s", accessor, tn.c_str(), sfx);
            }
        }
    }

    void emitDecodeList(ZCMMember& lm, const string& accessor_, int indent, bool isFirst,
                        const string& len_, bool fixedLen)
    {
        auto& tn = lm.type.fullname;
        const char *suffix = isFirst ? "" : ")";
        auto *accessor = accessor_.c_str();
        auto *len = len_.c_str();

        if (tn == "byte") {
            emit(indent, "%sbuf.read(%s%s)%s",
                  accessor, fixedLen ? "" : "self.", len, suffix);
        } else if (tn == "boolean") {
            if(fixedLen) {
                emit(indent, "%smap(bool, struct.unpack('>%s%c', buf.read(%d)))%s",
                     accessor, len, getStructFormat(lm),
                     atoi(len) * getPrimitiveTypeSize(tn),
                     suffix);
            } else {
                emit(indent,
                     "%smap(bool, struct.unpack('>%%d%c' %% self.%s, buf.read(self.%s)))%s",
                     accessor, getStructFormat(lm), len, len, suffix);
            }
        } else if (tn == "int8_t" || tn == "int16_t" || tn == "int32_t" || tn == "int64_t" ||
                   tn == "float"  || tn == "double") {
            if(fixedLen) {
                emit (indent, "%sstruct.unpack('>%s%c', buf.read(%d))%s",
                      accessor, len, getStructFormat(lm),
                      atoi(len) * getPrimitiveTypeSize(tn),
                      suffix);
            } else {
                if(getPrimitiveTypeSize(tn) > 1) {
                    emit(indent, "%sstruct.unpack('>%%d%c' %% self.%s, buf.read(self.%s * %d))%s",
                         accessor, getStructFormat(lm), len, len,
                         getPrimitiveTypeSize(tn), suffix);
                } else {
                    emit(indent, "%sstruct.unpack('>%%d%c' %% self.%s, buf.read(self.%s))%s",
                         accessor, getStructFormat(lm), len, len, suffix);
                }
            }
        } else {
            assert(0);
        }
    }

    void flushReadStructFmt(std::queue<int>& formats, std::queue<ZCMMember*>& members)
    {
        size_t nfmts = formats.size();
        assert(nfmts == members.size());
        if (nfmts == 0)
            return;

        emitStart(0, "        ");
        int fmtsize = 0;
        while (members.size() > 0) {
            auto *lm = members.front(); members.pop();
            emitContinue("self.%s", lm->membername.c_str());
            if (members.size() > 0)
                emitContinue (", ");
            fmtsize += getPrimitiveTypeSize(lm->type.fullname);
        }
        emitContinue (" = struct.unpack(\">");
        while (formats.size() > 0) {
            int f = formats.front(); formats.pop();
            emitContinue("%c", f);
        }
        emitEnd("\", buf.read(%d))%s", fmtsize, nfmts == 1 ? "[0]" : "");
    }

    void emitPythonDecodeOne()
    {
        emit(1, "def _decode_one(buf):");
        emit(2, "self = %s()", ls.structname.shortname.c_str());

        std::queue<int> structFmt;
        std::queue<ZCMMember*> structMembers;

        for (auto& lm : ls.members) {
            char fmt = getStructFormat(lm);

            if (lm.dimensions.size() == 0) {
                if (fmt && lm.type.fullname != "boolean") {
                    structFmt.push((int)fmt);
                    structMembers.push(&lm);
                } else {
                    flushReadStructFmt(structFmt, structMembers);
                    string accessor = "self." + lm.membername + " = ";
                    emitDecodeOne(lm, accessor.c_str(), 2, "");
                }
            } else {
                flushReadStructFmt(structFmt, structMembers);
                string accessor = "self." + lm.membername;

                // iterate through the dimensions of the member, building up
                // an accessor string, and emitting for loops
                uint n = 0;
                for (n = 0; n < lm.dimensions.size()-1; n++) {
                    auto& dim = lm.dimensions[n];

                    if(n == 0) {
                        emit(2, "%s = []", accessor.c_str());
                    } else {
                        emit(2+n, "%s.append([])", accessor.c_str());
                    }

                    if (dim.mode == ZCM_CONST) {
                        emit(2+n, "for i%d in range(%s):", n, dim.size.c_str());
                    } else {
                        emit(2+n, "for i%d in range(self.%s):", n, dim.size.c_str());
                    }

                    if(n > 0 && n < lm.dimensions.size()-1) {
                        accessor += "[i" + to_string(n-1) + "]";
                    }
                }

                // last dimension.
                auto& lastDim = lm.dimensions[lm.dimensions.size()-1];
                bool lastDimFixedLen = (lastDim.mode == ZCM_CONST);

                if (ZCMGen::isPrimitiveType(lm.type.fullname) &&
                    lm.type.fullname != "string") {
                    // member is a primitive non-string type.  Emit code to
                    // decode a full array in one call to struct.unpack
                    if(n == 0) {
                        accessor += " = ";
                    } else {
                        accessor += ".append(";
                    }

                    emitDecodeList(lm, accessor, 2+n, n==0,
                                   lastDim.size, lastDimFixedLen);
                } else {
                    // member is either a string type or an inner ZCM type.  Each
                    // array element must be decoded individually
                    if(n == 0) {
                        emit(2, "%s = []", accessor.c_str());
                    } else {
                        emit(2+n, "%s.append ([])", accessor.c_str());
                        accessor += "[i" + to_string(n-1) + "]";
                    }
                    if (lastDimFixedLen) {
                        emit(2+n, "for i%d in range(%s):", n, lastDim.size.c_str());
                    } else {
                        emit(2+n, "for i%d in range(self.%s):", n, lastDim.size.c_str());
                    }
                    accessor += ".append(";
                    emitDecodeOne(lm, accessor, n+3, ")");
                }
            }
        }
        flushReadStructFmt(structFmt, structMembers);
        emit(2, "return self");

        emit(1, "_decode_one = staticmethod(_decode_one)");
        emit(0, "");
    }

    void emitPythonDecode()
    {
        emit(1, "def decode(data):");
        emit(1, "    if hasattr(data, 'read'):");
        emit(1, "        buf = data");
        emit(1, "    else:");
        emit(1, "        buf = BytesIO(data)");
        emit(1, "    if buf.read(8) != %s._get_packed_fingerprint():", ls.structname.shortname.c_str());
        emit(1, "        raise ValueError(\"Decode error\")");
        emit(1, "    return %s._decode_one(buf)", ls.structname.shortname.c_str());
        emit(1, "decode = staticmethod(decode)");
        emit(0, "");
    }

    void emitEncodeOne(ZCMMember& lm, const string& accessor_, int indent)
    {
        const string& tn = lm.type.fullname;
        const string& mn = lm.membername;
        auto *accessor = accessor_.c_str();

        if (tn == "string") {
            emit(indent, "__%s_encoded = %s.encode('utf-8')", mn.c_str(), accessor);
            emit(indent, "buf.write(struct.pack('>I', len(__%s_encoded)+1))", mn.c_str());
            emit(indent, "buf.write(__%s_encoded)", mn.c_str());
            emit(indent, "buf.write(b\"\\0\")");
        } else if (tn == "byte") {
            emit(indent, "buf.write(struct.pack('B', %s))", accessor);
        } else if (tn == "int8_t" || tn == "boolean") {
            emit(indent, "buf.write(struct.pack('b', %s))", accessor);
        } else if (tn == "int16_t") {
            emit(indent, "buf.write(struct.pack('>h', %s))", accessor);
        } else if (tn == "int32_t") {
            emit(indent, "buf.write(struct.pack('>i', %s))", accessor);
        } else if (tn == "int64_t") {
            emit(indent, "buf.write(struct.pack('>q', %s))", accessor);
        } else if (tn == "float") {
            emit(indent, "buf.write(struct.pack('>f', %s))", accessor);
        } else if (tn == "double") {
            emit(indent, "buf.write(struct.pack('>d', %s))", accessor);
        } else {
            auto& sn = lm.type.shortname;
            auto *gpf = "_get_packed_fingerprint()";
            if (lm.type.fullname == ls.structname.fullname) {
                emit(indent, "assert %s.%s == %s.%s", accessor, gpf, sn.c_str(), gpf);
            } else {
                emit(indent, "assert %s.%s == %s.%s",
                     accessor, gpf, lm.type.fullname.c_str(), gpf);
            }
            emit(indent, "%s._encode_one(buf)", accessor);
        }
    }

    void emitEncodeList(ZCMMember& lm, const string& accessor_, int indent,
                        const string& len_, int fixedLen)
    {
        auto& tn = lm.type.fullname;
        auto *accessor = accessor_.c_str();
        auto *len = len_.c_str();

        if (tn == "byte") {
            emit(indent, "buf.write(bytearray(%s[:%s%s]))",
                 accessor, (fixedLen ? "" : "self."), len);
            return;
        } else if (tn == "boolean" || tn == "int8_t" || tn == "int16_t" || tn == "int32_t" ||
                   tn == "int64_t" || tn == "float"  || tn == "double") {
            if(fixedLen) {
                emit(indent, "buf.write(struct.pack('>%s%c', *%s[:%s]))",
                     len, getStructFormat(lm), accessor, len);
            } else {
                emit(indent, "buf.write(struct.pack('>%%d%c' %% self.%s, *%s[:self.%s]))",
                     getStructFormat(lm), len, accessor, len);
            }
        } else {
            assert(0);
        }
    }

    void flushWriteStructFmt(std::queue<int>& formats, std::queue<ZCMMember*>& members)
    {
        size_t nfmts = formats.size();
        assert(nfmts == members.size());
        if (nfmts == 0)
            return;

        emitStart(2, "buf.write(struct.pack(\">");
        while (formats.size() > 0) {
            int f = formats.front(); formats.pop();
            emitContinue("%c", f);
        }
        emitContinue ("\", ");
        while (members.size() > 0) {
            auto *lm = members.front(); members.pop();
            emitContinue("self.%s", lm->membername.c_str());
            if (members.size() > 0)
                emitContinue(", ");
        }
        emitEnd("))");
    }

    void emitPythonEncodeOne()
    {
        emit(1, "def _encode_one(self, buf):");
        if (ls.members.size() == 0) {
            emit(2, "pass");
            return;
        }

        std::queue<int> structFmt;
        std::queue<ZCMMember*> structMembers;

        for (auto& lm : ls.members) {
            char fmt = getStructFormat(lm);
            if (lm.dimensions.size() == 0) {
                if (fmt) {
                    structFmt.push((int)fmt);
                    structMembers.push(&lm);
                } else {
                    flushWriteStructFmt(structFmt, structMembers);
                    emitEncodeOne (lm, "self."+lm.membername, 2);
                }
            } else {
                flushWriteStructFmt(structFmt, structMembers);
                string accessor = "self." + lm.membername;
                unsigned int n;
                for (n = 0; n < lm.dimensions.size()-1; n++) {
                    auto& dim = lm.dimensions[n];
                    accessor += "[i" + to_string(n) + "]";
                    if (dim.mode == ZCM_CONST) {
                        emit(2+n, "for i%d in range(%s):", n, dim.size.c_str());
                    } else {
                        emit(2+n, "for i%d in range(self.%s):", n, dim.size.c_str());
                    }
                }

                // last dimension.
                auto& lastDim = lm.dimensions[lm.dimensions.size()-1];
                bool lastDimFixedLen = (lastDim.mode == ZCM_CONST);

                if (ZCMGen::isPrimitiveType(lm.type.fullname) &&
                    lm.type.fullname != "string") {
                    emitEncodeList(lm, accessor, 2+n, lastDim.size, lastDimFixedLen);
                } else {
                    if (lastDimFixedLen) {
                        emit(2+n, "for i%d in range(%s):", n, lastDim.size.c_str());
                    } else {
                        emit(2+n, "for i%d in range(self.%s):", n, lastDim.size.c_str());
                    }
                    accessor += "[i" + to_string(n) + "]";
                    emitEncodeOne(lm, accessor, n+3);
                }
            }
        }
        flushWriteStructFmt(structFmt, structMembers);

        emit(0, "");
    }

    void emitPythonEncode()
    {
        emit(1, "def encode(self):");
        emit(1, "    buf = BytesIO()");
        emit(1, "    buf.write(%s._get_packed_fingerprint())", ls.structname.shortname.c_str());
        emit(1, "    self._encode_one(buf)");
        emit(1, "    return buf.getvalue()");
        emit(0, "");
    }

    void emitMemberInitializer(ZCMMember& lm, int dimNum)
    {
        if((size_t)dimNum == lm.dimensions.size()) {
            auto& tn = lm.type.fullname;
            const char* initializer = nullptr;
            if (tn == "byte") initializer = "0";
            else if (tn == "boolean") initializer = "False";
            else if (tn == "int8_t")  initializer = "0";
            else if (tn == "int16_t") initializer = "0";
            else if (tn == "int32_t") initializer = "0";
            else if (tn == "int64_t") initializer = "0";
            else if (tn == "float")   initializer = "0.0";
            else if (tn == "double")  initializer = "0.0";
            else if (tn == "string")  initializer = "\"\"";

            if (initializer) {
                fprintfPass("%s", initializer);
            } else {
                fprintfPass("%s()", tn.c_str());
            }
            return;
        }
        // Arrays of bytes get treated as strings, so that they can be more
        // efficiently packed and unpacked.
        if ((size_t)dimNum == lm.dimensions.size() - 1 &&
            lm.type.fullname == "byte") {
            fprintfPass("\"\"");
            return;
        }
        auto& dim = lm.dimensions[dimNum];
        if (dim.mode == ZCM_VAR) {
            fprintfPass("[]");
        } else {
            fprintfPass("[ ");
            emitMemberInitializer(lm, dimNum+1);
            fprintfPass(" for dim%d in range(%s) ]", dimNum, dim.size.c_str());
        }
    }

    void emitPythonInit()
    {
        emit(1,"def __init__(self):");
        uint i;
        for (i = 0; i < ls.members.size(); i++) {
            auto& lm = ls.members[i];
            emitStart(1, "    self.%s = ", lm.membername.c_str());
            emitMemberInitializer(lm, 0);
            emitEnd("");
        }
        if (i == 0)
            emit(1,"    pass");
        emit(0, "");
    }

    void emitPythonFingerprint()
    {
        auto& sn_ = ls.structname.shortname;
        auto *sn = sn_.c_str();

        emit(1, "_hash = None");

        emit(1, "def _get_hash_recursive(parents):");
        emit(2,     "if %s in parents: return 0", sn);
        for (auto& lm : ls.members) {
            if (!ZCMGen::isPrimitiveType(lm.type.fullname)) {
                emit(2,     "newparents = parents + [%s]", sn);
                break;
            }
        }
        emitStart(2, "tmphash = (0x%" PRIx64, ls.hash);
        for (auto &lm : ls.members) {
            auto& msn = lm.type.shortname;
            if (!ZCMGen::isPrimitiveType(lm.type.fullname)) {
                const char *ghr = "_get_hash_recursive(newparents)";
                if (lm.type.fullname == ls.structname.fullname) {
                    emitContinue("+ %s.%s", msn.c_str(), ghr);
                } else {
                    if (lm.type.package != "")
                        emitContinue("+ %s.%s.%s", lm.type.package.c_str(), msn.c_str(), ghr);
                    else
                        emitContinue("+ %s.%s", msn.c_str(), ghr);
                }
            }
        }
        emitEnd (") & 0xffffffffffffffff");

        emit(2, "tmphash  = (((tmphash<<1)&0xffffffffffffffff)  + "
             "(tmphash>>63)) & 0xffffffffffffffff");
        emit (2,     "return tmphash");
        emit (1, "_get_hash_recursive = staticmethod(_get_hash_recursive)");

        emit (1, "_packed_fingerprint = None");
        emit (0, "");
        emit (1, "def _get_packed_fingerprint():");
        emit (2,     "if %s._packed_fingerprint is None:", sn);
        emit (3,         "%s._packed_fingerprint = struct.pack(\">Q\", "
              "%s._get_hash_recursive([]))", sn, sn);
        emit (2,     "return %s._packed_fingerprint", sn);
        emit (1, "_get_packed_fingerprint = staticmethod(_get_packed_fingerprint)");
        emit(0, "");

    }

    void emitPythonDependencies()
    {
        unordered_set<string> dependencies;
        for (auto& lm : ls.members) {
            auto& tn = lm.type.fullname;
            if (!ZCMGen::isPrimitiveType(tn) &&
                dependencies.find(tn) == dependencies.end() &&
                tn != ls.structname.fullname) {
                dependencies.insert(tn);
            }
        }

        for (auto& tn : dependencies) {
            emit(0, "from %s import %s", tn.c_str(), tn.c_str());
            emit(0,"");
        }
    }
};

struct PyEmitPack : public Emitter
{
    ZCMGen& zcm;

    PyEmitPack(ZCMGen& zcm, const string& fname):
        Emitter(fname), zcm(zcm) {}

    int emitPackage(const string& packName, vector<ZCMStruct*>& packStructs)
    {
        // create the package directory, if necessary
        vector<string> dirs = StringUtil::split(packName, '.');
        string pdname = StringUtil::join(dirs, '/');
        int havePackage = dirs.size() > 0;

        auto& ppath = zcm.gopt->getString("ppath");
        string packageDirPrefix = ppath + ((ppath.size() > 0) ? "/" : "");
        string packageDir = packageDirPrefix + pdname + (havePackage ? "/" : "");

        if (packageDir != "") {
            if (!FileUtil::exists(packageDir)) {
                FileUtil::mkdirWithParents(packageDir, 0755);
            }
            if (!FileUtil::dirExists(packageDir)) {
                cerr << "Could not create directory " << packageDir << "\n";
                return -1;
            }
        }

        // write the package __init__.py files, if necessary
        FILE *initPyFp = nullptr;
        unordered_set<string> initPyImports;

        if (havePackage) {
            int ndirs = dirs.size();
            for (int i=0 ; i<ndirs; i++) {

                vector<string> initPyFnameParts;
                initPyFnameParts.push_back(packageDirPrefix);
                for (int j = 0; j <= i; j++)
                    initPyFnameParts.push_back(dirs[j]);
                initPyFnameParts.push_back("__init__.py");

                string initpyFname = StringUtil::join(initPyFnameParts, '/');
                bool createdInitpy = false;

                // close init_py_fp if already open
                if (initPyFp) {
                    fclose(initPyFp);
                    initPyFp = nullptr;
                }

                if (!FileUtil::exists(initpyFname)) {
                    // __init__.py does not exist for this package.  Create it.
                    createdInitpy = true;
                    initPyFp = fopen(initpyFname.c_str(), "w");
                } else {
                    // open the existing __init__.py file, and make note of the
                    // modules it imports
                    createdInitpy = 0;
                    initPyFp = fopen(initpyFname.c_str(), "r+");
                }

                if (!initPyFp) {
                    perror("fopen");
                    return -1;
                }

                //////////////////////////////////////////////////////////////////
                //////////////////////////////////////////////////////////////////
                //////////////////////////////////////////////////////////////////

                if(createdInitpy) {
                    fprintf(initPyFp, "\"\"\"ZCM package __init__.py file\n"
                             "This file automatically generated by zcm-gen.\n"
                             "DO NOT MODIFY BY HAND!!!!\n"
                             "\"\"\"\n\n");
                } else {
                    while(!feof(initPyFp)) {
                        char buf[4096];
                        memset(buf, 0, sizeof(buf));
                        char *result = fgets(buf, sizeof(buf)-1, initPyFp);
                        if (!result)
                            break;
                        auto words = StringUtil::split(StringUtil::strip(buf), ' ');
                        if (words.size() < 4)
                            continue;
                        if (words[0] == "from" && words[2] == "import") {
                            string module = string(words[1].c_str()+1);
                            initPyImports.insert(std::move(module));
                        }
                    }
                }
            }
        }

        ////////////////////////////////////////////////////////////
        // STRUCTS
        for (auto *ls_ : packStructs) {
            auto& ls = *ls_;
            auto& sn_ = ls.structname.shortname;
            auto *sn = sn_.c_str();
            string path = packageDir + sn_ + ".py";

            if(initPyFp && initPyImports.find(sn_) == initPyImports.end()) {
                fprintf(initPyFp, "from .%s import %s\n", sn, sn);
            }

            if (!zcm.needsGeneration(ls.zcmfile, path))
                continue;

            PyEmitStruct{zcm, ls, path}.emitStruct();
        }

        if(initPyFp)
            fclose(initPyFp);

        return 0;
    }
};

int emitPython(ZCMGen& zcm)
{
    if (zcm.gopt->getBool("little-endian-encoding")) {
        printf("Python does not currently support little endian encoding\n");
        return -1;
    }

    unordered_map<string, vector<ZCMStruct*> > packages;

    // group the structs by package
    for (auto& ls : zcm.structs)
        packages[ls.structname.package].push_back(&ls);

    for (auto& kv : packages) {
        auto& name = kv.first;
        auto& pack = kv.second;
        int ret = PyEmitPack{zcm, name}.emitPackage(name, pack);
        if (ret != 0) return ret;
    }

    return 0;
}
