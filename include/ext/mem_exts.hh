#ifndef MEM_EXTS_HH
#define MEM_EXTS_HH

#include "core/cmd.hh"
#include <tlm>

// ReadCmd 的 TLM 扩展
class ReadCmdExt : public tlm::tlm_extension<ReadCmdExt>
{
public:
    typedef ReadCmd data_t;
    ReadCmd data;

    ReadCmdExt() {}
    ReadCmdExt(const ReadCmd& d) : data(d) {}

    virtual tlm_extension* clone() const {
        return new ReadCmdExt(data);
    }

    virtual void copy_from(const tlm_extension_base& ext) {
        data = static_cast<const ReadCmdExt&>(ext).data;
    }

    virtual void print(std::ostream& os) const {
        os << "ReadCmdExt = " << data;
    }
};

// WriteCmd 的 TLM 扩展
class WriteCmdExt : public tlm::tlm_extension<WriteCmdExt>
{
public:
    typedef WriteCmd data_t;
    WriteCmd data;

    WriteCmdExt() {}
    WriteCmdExt(const WriteCmd& d) : data(d) {}

    virtual tlm_extension* clone() const {
        return new WriteCmdExt(data);
    }

    virtual void copy_from(const tlm_extension_base& ext) {
        data = static_cast<const WriteCmdExt&>(ext).data;
    }

    virtual void print(std::ostream& os) const {
        os << "WriteCmdExt = " << data;
    }
};

// WriteData 的 TLM 扩展
class WriteDataExt : public tlm::tlm_extension<WriteDataExt>
{
public:
    typedef WriteData data_t;
    WriteData data;

    WriteDataExt() {}
    WriteDataExt(const WriteData& d) : data(d) {}

    virtual tlm_extension* clone() const {
        return new WriteDataExt(data);
    }

    virtual void copy_from(const tlm_extension_base& ext) {
        data = static_cast<const WriteDataExt&>(ext).data;
    }

    virtual void print(std::ostream& os) const {
        os << "WriteDataExt = " << data;
    }
};

// ReadResp 的 TLM 扩展
class ReadRespExt : public tlm::tlm_extension<ReadRespExt>
{
public:
    typedef ReadResp data_t;
    ReadResp data;

    ReadRespExt() {}
    ReadRespExt(const ReadResp& d) : data(d) {}

    virtual tlm_extension* clone() const {
        return new ReadRespExt(data);
    }

    virtual void copy_from(const tlm_extension_base& ext) {
        data = static_cast<const ReadRespExt&>(ext).data;
    }

    virtual void print(std::ostream& os) const {
        os << "ReadRespExt = " << data;
    }
};

// WriteResp 的 TLM 扩展
class WriteRespExt : public tlm::tlm_extension<WriteRespExt>
{
public:
    typedef WriteResp data_t;
    WriteResp data;

    WriteRespExt() {}
    WriteRespExt(const WriteResp& d) : data(d) {}

    virtual tlm_extension* clone() const {
        return new WriteRespExt(data);
    }

    virtual void copy_from(const tlm_extension_base& ext) {
        data = static_cast<const WriteRespExt&>(ext).data;
    }

    virtual void print(std::ostream& os) const {
        os << "WriteRespExt = " << data;
    }
};

// StreamUniqID 的 TLM 扩展
class StreamIDExt : public tlm::tlm_extension<StreamIDExt>
{
public:
    typedef StreamUniqID data_t;
    StreamUniqID data;

    StreamIDExt() {}
    StreamIDExt(const StreamUniqID& d) : data(d) {}

    virtual tlm_extension* clone() const {
        return new StreamIDExt(data);
    }

    virtual void copy_from(const tlm_extension_base& ext) {
        data = static_cast<const StreamIDExt&>(ext).data;
    }

    virtual void print(std::ostream& os) const {
        os << "StreamIDExt = " << data;
    }
};

#endif // MEM_EXTS_HH