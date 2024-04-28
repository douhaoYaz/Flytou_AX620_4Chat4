<template>
  <div>
    <div class="video-preview-container"></div>
    <div>
      <el-form ref="formVideoRef" :rules="formVideoRules" :model="formVideo" label-width="100px" label-position="left">
        <el-form-item label="主码流0">
        </el-form-item>
        <el-form-item label="" label-width="35px">
          <el-form-item label="码流控制">
            <el-select v-model="formVideo.video0.rc_type">
              <el-option v-for="item in formVideo.rc_type_options" :key="item.value" :label="item.label" :value="item.value"></el-option>
            </el-select>
          </el-form-item>
          <el-row>
            <el-col :span="12">
              <el-form-item label="码流平滑" v-show="formVideo.video0.rc_type === 0">
                <el-slider v-model="formVideo.video0.max_iprop" :min=formVideo.video0.min_iprop show-input>
                </el-slider>
              </el-form-item>
            </el-col>
          </el-row>
        </el-form-item>
        <el-form-item label="子码流1">
        </el-form-item>
        <el-form-item label="" label-width="35px">
          <el-form-item label="码流控制">
            <el-select v-model="formVideo.video1.rc_type">
              <el-option v-for="item in formVideo.rc_type_options" :key="item.value" :label="item.label" :value="item.value"></el-option>
            </el-select>
          </el-form-item>
          <el-row>
            <el-col :span="12">
              <el-form-item label="码流平滑" v-show="formVideo.video1.rc_type === 0">
                <el-slider v-model="formVideo.video1.max_iprop" :min=formVideo.video1.min_iprop show-input>
                </el-slider>
              </el-form-item>
            </el-col>
          </el-row>
        </el-form-item>
        <el-form-item label="" label-width="0px">
          <el-button type="primary" @click="onSubmit">修改</el-button>
        </el-form-item>
      </el-form>
    </div>
  </div>
</template>

<script>
export default {
  data () {
    return {
      formVideo: {
        rc_type_options:[
          {
            label: 'CBR',
            value: 0
          },
          {
            label: 'VBR',
            value: 1
          },
          {
            label: 'FIXQP',
            value: 2
          }
        ],
        video0: {
          rc_type: 0,
          min_iprop: 10,
          max_iprop: 40
        },
        video1: {
          rc_type: 0,
          min_iprop: 10,
          max_iprop: 40
        }
      },
      formVideoRules: {
      }
    }
  },
  created () {
    console.log('video++')
    this.getInfo()
  },
  methods: {
    async onSubmit () {
      try {
        var objData = {}
        objData = JSON.parse(JSON.stringify(this.formVideo))
        var _uri = 'setting/video'
        const { data: res } = await this.$http.post(_uri, objData)
        console.log('video get return: ', res)
        if (res.meta.status === 200) {
          this.$message.success('修改成功')
        } else {
          this.$message.success('修改失败')
        }
      } catch (error) {
        this.$message.error('修改失败')
      }
    },
    async getInfo () {
      try {
        var _uri = 'setting/video'
        const { data: res } = await this.$http.get(_uri)
        console.log('video get return: ', res)
        if (res.meta.status === 200) {
          this.formVideo.video0 = res.data.video0
          this.formVideo.video1 = res.data.video1
        }
      } catch (error) {
        this.$message.error('获取信息失败')
      }
    }
  }
}
</script>

<style lang="less" scoped>
.el-input-number {
  width: 120px;
}
.el-select {
  width: 100px;
}
.inline {
  display: inline-block;
 }
</style>
